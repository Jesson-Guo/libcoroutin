#ifndef IO_SERVICE_H
#define IO_SERVICE_H

#include "../on_scope_exit.h"
#include "../cancellation/cancellation_token.h"
#include "../cancellation/cancellation_registration.h"
#include "../detail/macos.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

typedef int DWORD;
#define INFINITE ((DWORD)-1) // 用于在 timer_thread_state::run() 中设置超时值
typedef long long int LONGLONG;

namespace coro {

class io_service {
public:
    class schedule_operation;
    class timed_schedule_operation;

    io_service() : io_service(10) {}

    /// 使用并发提示初始化 io_service。
    ///
    /// \param queue_length
    /// 指定正在积极处理事件的 I/O 线程的目标最大数量。
    /// 注意，活动线程的数量可能会暂时超过此数量。
    explicit io_service(const size_t queue_length)
        : m_mq(new detail::macos::message_queue(queue_length))
        , m_thread_state(0)
        , m_work_count(0)
        , m_schedule_operations(nullptr)
        , m_timer_state(nullptr) {}

    ~io_service() {
        assert(m_schedule_operations.load(std::memory_order_relaxed) == nullptr);
        assert(m_thread_state.load(std::memory_order_relaxed) < active_thread_count_increment);
        delete m_timer_state.load(std::memory_order_relaxed);
        delete m_mq;
    }

    io_service(io_service&& other) = delete;
    io_service(const io_service& other) = delete;
    io_service& operator=(io_service&& other) = delete;
    io_service& operator=(const io_service& other) = delete;

    /// 返回一个操作，当被等待时，将挂起等待的协程，并将其重新调度到与此 io_service 关联的 I/O 线程上。
    [[nodiscard]] schedule_operation schedule() noexcept;

    /// 返回一个操作，当被等待时，将挂起等待的协程指定的延迟时间。
    /// 一旦延迟时间过去，协程将在与此 io_service 关联的 I/O 线程上恢复执行。
    template<typename REP, typename PERIOD>
    [[nodiscard]] timed_schedule_operation schedule_after(
        const std::chrono::duration<REP, PERIOD>& delay,
        cancellation_token cancellation_token = {}) noexcept;

    /// 处理事件直到 io_service 被停止。
    std::uint64_t process_events() {
        std::uint64_t event_count = 0;
        if (try_enter_event_loop()) {
            auto exit_loop = on_scope_exit([&] { exit_event_loop(); });
            constexpr bool wait_for_event = true;
            while (try_process_one_event(wait_for_event)) {
                ++event_count;
            }
        }
        return event_count;
    }

    /// 处理事件直到 io_service 被停止或队列中没有更多的待处理事件。
    std::uint64_t process_pending_events() {
        std::uint64_t event_count = 0;
        if (try_enter_event_loop()) {
            auto exit_loop = on_scope_exit([&] { exit_event_loop(); });
            constexpr bool wait_for_event = false;
            while (try_process_one_event(wait_for_event)) {
                ++event_count;
            }
        }
        return event_count;
    }

    /// 阻塞直到处理一个事件或 io_service 被停止。
    std::uint64_t process_one_event() {
        std::uint64_t event_count = 0;
        if (try_enter_event_loop()) {
            auto exit_loop = on_scope_exit([&] { exit_event_loop(); });
            if (try_process_one_event(true)) {
                ++event_count;
            }
        }
        return event_count;
    }

    /// 如果有任何事件待处理，则处理一个事件，否则如果没有待处理事件或 io_service 被停止，则立即返回。
    std::uint64_t process_one_pending_event() {
        std::uint64_t event_count = 0;
        if (try_enter_event_loop()) {
            auto exit_loop = on_scope_exit([&] { exit_event_loop(); });
            if (try_process_one_event(false)) {
                ++event_count;
            }
        }
        return event_count;
    }

    /// 关闭 io_service
    void stop() noexcept {
        if (const auto old_state = m_thread_state.fetch_or(stop_requested_flag, std::memory_order_release);
            (old_state & stop_requested_flag) == 0) {
            // 唤醒所有正在等待事件的 I/O 线程，让它们及时退出事件循环。
            for (auto active_thread_count = old_state / active_thread_count_increment;
                active_thread_count > 0;
                --active_thread_count) {
                post_wake_up_event();
            }
        }
    }

    /// 重置 io_service 以准备恢复事件处理。
    void reset() {
        const auto old_state = m_thread_state.fetch_and(~stop_requested_flag, std::memory_order_relaxed);
        // 检查是否没有活动线程正在运行事件循环。
        assert(old_state == stop_requested_flag);
    }

    /// 检测 io_service 是否被请求停止
    bool is_stop_requested() const noexcept {
        return (m_thread_state.load(std::memory_order_acquire) & stop_requested_flag) != 0;
    }

    /// 通知 io_service 新任务的开始，增加工作计数。
    void notify_work_started() noexcept {
        m_work_count.fetch_add(1, std::memory_order_relaxed);
    }

    /// 通知 io_service 某个工作任务已完成，减少工作计数。
    void notify_work_finished() noexcept {
        if (m_work_count.fetch_sub(1, std::memory_order_relaxed) == 1) {
            stop();
        }
    }

private:
    class timer_thread_state;
    class timer_queue;

    friend class schedule_operation;
    friend class timed_schedule_operation;

    void schedule_impl(schedule_operation* operation) noexcept;

    void try_reschedule_overflow_operations() noexcept;

    /// 尝试让线程进入事件循环，并对 io_service 中活动线程的计数进行管理。
    bool try_enter_event_loop() noexcept {
        auto current_state = m_thread_state.load(std::memory_order_relaxed);
        do {
            if ((current_state & stop_requested_flag) != 0) {
                return false;
            }
        } while (!m_thread_state.compare_exchange_weak(
            current_state,
            current_state + active_thread_count_increment,
            std::memory_order_relaxed));
        return true;
    }

    void exit_event_loop() noexcept {
        m_thread_state.fetch_sub(active_thread_count_increment, std::memory_order_relaxed);
    }

    /// 处理单个事件，并根据传入的参数决定是否等待事件的到来。
    ///
    /// \param wait_for_event
    /// true:  表示调用 dequeue_message 时，如果队列中没有事件，应该阻塞等待直到有事件到达。
    /// false: 表示 dequeue_message 立即返回，如果没有事件可处理则返回 false。
    bool try_process_one_event(const bool wait_for_event) {
        if (is_stop_requested()) {
            return false;
        }

        while (true) {
            try_reschedule_overflow_operations();
            // 指向消息的指针，通常是一个协程句柄地址或 I/O 操作的状态对象
            void* message = nullptr;
            detail::macos::message_type type = detail::macos::RESUME_TYPE;

            // 调用 dequeue_message 从 m_mq 中取出一个消息（或事件）
            if (const bool status = m_mq->dequeue_message(message, type, wait_for_event);
                !status) {
                return false;
            }

            // I/O 回调事件
            if (type == detail::macos::CALLBACK_TYPE) {
                auto* state = static_cast<detail::macos::io_state*>(message);
                state->m_callback(state);
                return true;
            }

            // 恢复协程事件
            if (reinterpret_cast<uintptr_t>(message) != 0) {
                std::coroutine_handle<>::from_address(message).resume();
                return true;
            }

            if (is_stop_requested()) {
                return false;
            }
        }
    }

    /// 唤醒可能处于阻塞状态的 I/O 线程，使其能够及时地重新进入事件循环，处理队列中的待处理任务或溢出的操作。
    void post_wake_up_event() const noexcept {
        // 向消息队列中加入一个特殊的唤醒事件，唤醒阻塞在 kevent 上的线程。
        (void)m_mq->enqueue_message(nullptr, detail::macos::RESUME_TYPE);
    }

    timer_thread_state* ensure_timer_thread_started();

    static constexpr std::uint32_t stop_requested_flag = 1;
    static constexpr std::uint32_t active_thread_count_increment = 2;

    detail::macos::message_queue* m_mq;

    // 位 0: stop_requested_flag
    // 位 1-31: 当前正在运行事件循环的活动线程数
    std::atomic<std::uint32_t> m_thread_state;
    std::atomic<std::uint32_t> m_work_count;

    // 准备运行但未能加入消息队列的调度操作的链表头
    std::atomic<schedule_operation*> m_schedule_operations;
    std::atomic<timer_thread_state*> m_timer_state;
};

class io_service::schedule_operation {
public:
    explicit schedule_operation(io_service& service) noexcept
        : m_service(service)
        , m_next(nullptr) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(const std::coroutine_handle<> awaiter) noexcept {
        m_awaiter = awaiter;
        m_service.schedule_impl(this);
    }

    void await_resume() const noexcept {}

private:
    friend class io_service;
    friend class timed_schedule_operation;

    io_service& m_service;
    std::coroutine_handle<> m_awaiter;
    schedule_operation* m_next;
};

class io_service::timed_schedule_operation {
public:
    using time_point = std::chrono::high_resolution_clock::time_point;

    timed_schedule_operation(io_service& service, const time_point resume_time, cancellation_token token) noexcept
        : m_schedule_operation(service)
        , m_resume_time(resume_time)
        , m_cancellation_token(std::move(token))
        , m_next(nullptr)
        , m_ref_count(2) {}

    timed_schedule_operation(timed_schedule_operation&& other) noexcept
        : m_schedule_operation(other.m_schedule_operation)
        , m_resume_time(other.m_resume_time)
        , m_cancellation_token(std::move(other.m_cancellation_token))
        , m_next(nullptr)
        , m_ref_count(2) {}

    ~timed_schedule_operation() = default;

    timed_schedule_operation& operator=(timed_schedule_operation&& other) = delete;
    timed_schedule_operation(const timed_schedule_operation& other) = delete;
    timed_schedule_operation& operator=(const timed_schedule_operation& other) = delete;

    bool await_ready() const noexcept {
        // 如果任务被取消，则返回 true，不需要挂起，协程立即恢复。
        return m_cancellation_token.is_cancellation_requested();
    }

    void await_suspend(std::coroutine_handle<> awaiter);

    void await_resume() {
        // 清理 m_cancellation_registration，即取消注册之前的取消处理函数。
        m_cancellation_registration.reset();
        m_cancellation_token.throw_if_cancellation_requested();
    }

private:
    friend class timer_queue;
    friend class timer_thread_state;

    schedule_operation m_schedule_operation;
    time_point m_resume_time;
    cancellation_token m_cancellation_token;
    std::optional<cancellation_registration> m_cancellation_registration;
    timed_schedule_operation* m_next;
    std::atomic<std::uint32_t> m_ref_count;
};

class io_work_scope {
public:
    explicit io_work_scope(io_service& service) noexcept : m_service(&service) {
        service.notify_work_started();
    }

    io_work_scope(const io_work_scope& other) noexcept : m_service(other.m_service) {
        if (m_service) {
            m_service->notify_work_started();
        }
    }

    io_work_scope(io_work_scope&& other) noexcept : m_service(other.m_service) {
        other.m_service = nullptr;
    }

    ~io_work_scope() {
        if (m_service) {
            m_service->notify_work_finished();
        }
    }

    void swap(io_work_scope& other) noexcept {
        std::swap(m_service, other.m_service);
    }

    io_work_scope& operator=(io_work_scope other) noexcept {
        swap(other);
        return *this;
    }

    [[nodiscard]] io_service& service() const noexcept {
        return *m_service;
    }

private:
    io_service* m_service;
};

}

/// 利用堆排序的条目向量和一个额外的排序链表，当向量中没有足够的内存来存储所有计时器条目时，可以用作后备。
class coro::io_service::timer_queue {
public:
    using time_point = std::chrono::high_resolution_clock::time_point;

    timer_queue() noexcept : m_overflow_timers(nullptr) {}

    ~timer_queue() {
        assert(is_empty());
    }

    bool is_empty() const noexcept {
        return m_timer_entries.empty() && m_overflow_timers == nullptr;
    }

    [[nodiscard]] time_point earliest_due_time() const noexcept {
        if (!m_timer_entries.empty()) {
            if (m_overflow_timers) {
                return std::min(m_timer_entries.front().m_due_time, m_overflow_timers->m_resume_time);
            }
            return m_timer_entries.front().m_due_time;
        }
        if (m_overflow_timers) {
            return m_overflow_timers->m_resume_time;
        }
        return time_point::max();
    }

    void enqueue_timer(timed_schedule_operation* timer) noexcept {
        try {
            m_timer_entries.emplace_back(timer);
            std::ranges::push_heap(m_timer_entries, compare_entries);
        }
        catch (...) {
            // 堆内存不足，将任务加入溢出链表
            const auto& new_due_time = timer->m_resume_time;
            auto** current = &m_overflow_timers;
            while (*current && (*current)->m_resume_time <= new_due_time) {
                current = &(*current)->m_next;
            }
            timer->m_next = *current;
            *current = timer;
        }
    }

    void dequeue_due_timers(time_point current_time, timed_schedule_operation*& timer_list) noexcept {
        while (!m_timer_entries.empty() && m_timer_entries.front().m_due_time <= current_time) {
            auto* timer = m_timer_entries.front().m_timer;
            std::ranges::pop_heap(m_timer_entries, compare_entries);
            m_timer_entries.pop_back();

            // 将任务加入到准备执行的链表中
            timer->m_next = timer_list;
            timer_list    = timer;
        }

        // 处理链表中的到期任务
        while (m_overflow_timers && m_overflow_timers->m_resume_time <= current_time) {
            auto* timer = m_overflow_timers;
            m_overflow_timers = timer->m_next;
            timer->m_next = timer_list;
            timer_list = timer;
        }
    }

    void remove_cancelled_timers(timed_schedule_operation*& timer_list) noexcept {
        // 从堆中移除取消的任务
        const auto add_timer_to_list = [&](timed_schedule_operation* timer) {
            timer->m_next = timer_list;
            timer_list = timer;
        };

        const auto is_timer_cancelled = [](const timer_entry& entry) {
            return entry.m_timer->m_cancellation_token.is_cancellation_requested();
        };

        if (const auto first_cancelled_entry = std::ranges::find_if(m_timer_entries, is_timer_cancelled);
            first_cancelled_entry != m_timer_entries.end()) {
            auto non_cancelled_end = first_cancelled_entry;
            add_timer_to_list(non_cancelled_end->m_timer);

            for (auto iter = first_cancelled_entry + 1; iter != m_timer_entries.end(); ++iter) {
                if (is_timer_cancelled(*iter)) {
                    add_timer_to_list(iter->m_timer);
                }
                else {
                    *non_cancelled_end++ = *iter;
                }
            }

            m_timer_entries.erase(non_cancelled_end, m_timer_entries.end());
            std::ranges::make_heap(m_timer_entries, compare_entries);
        }

        // 从链表中移除取消的任务
        timed_schedule_operation** current = &m_overflow_timers;
        while (*current) {
            auto* timer = *current;
            if (timer->m_cancellation_token.is_cancellation_requested()) {
                *current = timer->m_next;
                add_timer_to_list(timer);
            }
            else {
                current = &timer->m_next;
            }
        }
    }

private:
    struct timer_entry {
        time_point m_due_time;
        timed_schedule_operation* m_timer;
        explicit timer_entry(timed_schedule_operation* timer)
            : m_due_time(timer->m_resume_time)
            , m_timer(timer) {}
    };

    static bool compare_entries(const timer_entry& a, const timer_entry& b) noexcept {
        return a.m_due_time > b.m_due_time;
    }

    // 一个堆排序的活动计时器条目列表
    std::vector<timer_entry> m_timer_entries;

    // 溢出计时器条目的链表，用于 m_timerEntries 没有足够的内存时。是一个有序链表
    timed_schedule_operation* m_overflow_timers;
};

class coro::io_service::timer_thread_state {
public:
    timer_thread_state()
        : m_kqueue_fd(detail::macos::create_kqueue_fd())
        , m_newly_queued_timers(nullptr)
        , m_timer_cancellation_requested(false)
        , m_shutdown_requested(false)
        , m_thread([this] { this->run(); }) {
        // 注册唤醒事件
        struct kevent kev{};
        EV_SET(&kev, 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
        if (kevent(m_kqueue_fd.fd(), &kev, 1, nullptr, 0, nullptr) == -1) {
            throw std::system_error{errno, std::system_category(), "Error registering wakeup event with kqueue"};
        }
    }

    ~timer_thread_state() {
        m_shutdown_requested.store(true, std::memory_order_release);
        wake_up_timer_thread();
        m_thread.join();
    }

    timer_thread_state(const timer_thread_state& other) = delete;
    timer_thread_state& operator=(const timer_thread_state& other) = delete;

    void request_timer_cancellation() noexcept {
        // 检查当前是否已有取消请求
        const bool timer_cancellation_already_requested =
            m_timer_cancellation_requested.exchange(true, std::memory_order_release);

        // 如果之前没有请求过取消，则唤醒定时器线程
        if (!timer_cancellation_already_requested) {
            wake_up_timer_thread();
        }
    }

    void run() {
        using clock = std::chrono::high_resolution_clock;
        using time_point = clock::time_point;

        timer_queue timer_queue;
        timed_schedule_operation* timers_ready_to_resume = nullptr;

        while (!m_shutdown_requested.load(std::memory_order_relaxed)) {
            struct kevent event{};
            timespec timeout_spec{};
            timespec* p_timeout = nullptr;

            // 设置超时时间：如果定时器队列不为空，设置超时时间为最早任务的到期时间
            if (!timer_queue.is_empty()) {
                time_point current_time = clock::now();
                if (auto earliest_due_time = timer_queue.earliest_due_time();
                    earliest_due_time <= current_time) {
                    // 立即处理到期的任务
                    timeout_spec.tv_sec = 0;
                    timeout_spec.tv_nsec = 0;
                }
                else {
                    // 设置等待时间，直到最早任务到期
                    auto duration = earliest_due_time - current_time;
                    timeout_spec.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
                    timeout_spec.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() % 1000000000;
                }
                p_timeout = &timeout_spec;
            }

            // 调用 kevent()，等待事件发生或定时器超时
            const int nev = kevent(m_kqueue_fd.fd(), nullptr, 0, &event, 1, p_timeout);
            if (nev == -1) {
                // 中断
                if (errno == EINTR) {
                    continue;
                }
                // 处理错误
                throw std::system_error{errno, std::system_category(), "Error in timer thread: kevent wait"};
            }
            if (nev == 0) {
                // 超时，处理到期的计时器
                if (!timer_queue.is_empty()) {
                    time_point current_time = clock::now();
                    timer_queue.dequeue_due_timers(current_time, timers_ready_to_resume);
                }
            }
            else {
                if (event.filter == EVFILT_USER) {
                    // 处理已取消的计时器
                    if (m_timer_cancellation_requested.exchange(false, std::memory_order_acquire)) {
                        timer_queue.remove_cancelled_timers(timers_ready_to_resume);
                    }

                    // 处理新加入的计时器
                    auto* new_timers = m_newly_queued_timers.exchange(nullptr, std::memory_order_acquire);
                    while (new_timers) {
                        auto* timer = new_timers;
                        new_timers = timer->m_next;
                        if (timer->m_cancellation_token.is_cancellation_requested()) {
                            // 如果新加入的任务已被取消，直接将其加入到准备恢复的队列
                            timer->m_next = timers_ready_to_resume;
                            timers_ready_to_resume = timer;
                        }
                        else {
                            // 否则，将其加入到定时器队列中
                            timer_queue.enqueue_timer(timer);
                        }
                    }
                }
            }

            // 恢复任何准备运行的定时器任务
            while (timers_ready_to_resume) {
                auto* timer = timers_ready_to_resume;
                timers_ready_to_resume = timer->m_next;

                if (timer->m_ref_count.fetch_sub(1, std::memory_order_release) == 1) {
                    // 恢复协程执行
                    timer->m_schedule_operation.m_service.schedule_impl(&timer->m_schedule_operation);
                }
            }
        }
    }

    void wake_up_timer_thread() const noexcept {
        struct kevent kev{};
        EV_SET(&kev, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
        kevent(m_kqueue_fd.fd(), &kev, 1, nullptr, 0, nullptr);
    }

    detail::macos::safe_fd m_kqueue_fd;

    std::atomic<timed_schedule_operation*> m_newly_queued_timers;
    std::atomic<bool> m_timer_cancellation_requested;
    std::atomic<bool> m_shutdown_requested;
    std::thread m_thread;
};

inline coro::io_service::schedule_operation coro::io_service::schedule() noexcept {
    return schedule_operation{*this};
}

template<typename REP, typename PERIOD>
coro::io_service::timed_schedule_operation coro::io_service::schedule_after(
    const std::chrono::duration<REP, PERIOD>& delay, cancellation_token cancellation_token) noexcept {
    return timed_schedule_operation{
        *this,
        std::chrono::high_resolution_clock::now() + delay,
        std::move(cancellation_token)
    };
}

inline void coro::io_service::schedule_impl(schedule_operation* operation) noexcept {
    // 尝试将调度操作加入消息队列
    if (const bool ok = m_mq->enqueue_message(operation->m_awaiter.address(), detail::macos::RESUME_TYPE);
        !ok) {
        // 无法发送到消息队列
        // 这很可能是因为队列当前已满
        // 我们将操作加入到一个无锁的链表中，并推迟调度到消息队列，直到某个 I/O 线程下次进入其事件循环
        auto* head = m_schedule_operations.load(std::memory_order_acquire);
        do {
            operation->m_next = head;
        } while (!m_schedule_operations.compare_exchange_weak(
            head,
            operation,
            std::memory_order_release,
            std::memory_order_acquire));
    }
}

inline void coro::io_service::try_reschedule_overflow_operations() noexcept {
    auto* operation = m_schedule_operations.exchange(nullptr, std::memory_order_acquire);
    // 循环遍历溢出操作链表，尝试将每一个 schedule_operation 加入消息队列。
    while (operation) {
        auto* next = operation->m_next;
        if (const bool ok = m_mq->enqueue_message(operation->m_awaiter.address(), detail::macos::RESUME_TYPE);
            !ok) {
            // 仍然无法将这些操作加入队列，将它们放回溢出操作列表中
            auto* tail = operation;
            while (tail->m_next) {
                tail = tail->m_next;
            }
            schedule_operation* head = nullptr;
            while (!m_schedule_operations.compare_exchange_weak(
                head,
                operation,
                std::memory_order_release,
                std::memory_order_relaxed)) {
                tail->m_next = head;
                }
            return;
        }
        operation = next;
    }
}

inline void coro::io_service::timed_schedule_operation::await_suspend(std::coroutine_handle<> awaiter) {
    m_schedule_operation.m_awaiter = awaiter;

    auto& service = m_schedule_operation.m_service;

    // 确保计时器状态已初始化并启动计时器线程
    auto* timer_state = service.ensure_timer_thread_started();

    if (m_cancellation_token.can_be_cancelled()) {
        // 注册取消处理函数
        m_cancellation_registration.emplace(m_cancellation_token, [timer_state] {
            // 该函数用于通知定时器线程：有定时任务被取消了，需要从定时器队列中移除这些任务。
            timer_state->request_timer_cancellation();
        });
    }
=
    // 将计时器调度加入到新计时器的队列中
    auto* prev = timer_state->m_newly_queued_timers.load(std::memory_order_acquire);
    do {
        m_next = prev;
    } while (!timer_state->m_newly_queued_timers.compare_exchange_weak(
        prev,
        this,
        std::memory_order_release,
        std::memory_order_acquire));

    // 唤醒定时器线程
    if (!prev) {
        timer_state->wake_up_timer_thread();
    }

    // 如果引用计数减少为1，表示可以调度任务了
    if (m_ref_count.fetch_sub(1, std::memory_order_acquire) == 1) {
        service.schedule_impl(&m_schedule_operation);
    }
}

inline coro::io_service::timer_thread_state* coro::io_service::ensure_timer_thread_started() {
    auto* timer_state = m_timer_state.load(std::memory_order_acquire);
    if (!timer_state) {
        auto new_timer_state = std::make_unique<timer_thread_state>();
        if (m_timer_state.compare_exchange_strong(
            timer_state,
            new_timer_state.get(),
            std::memory_order_release,
            std::memory_order_acquire)) {
            // 成功设置 timer_thread_state，其他线程不会覆盖
            // 不在这里释放，之后会在 io_service 析构时释放
            timer_state = new_timer_state.release();
        }
    }
    return timer_state;
}

#endif // IO_SERVICE_H
