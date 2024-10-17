//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/io/io_service.h"

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

    time_point earliest_due_time() const noexcept {
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
            timer_list = timer;
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

coro::io_service::schedule_operation coro::io_service::schedule() noexcept {
    return schedule_operation{*this};
}

std::uint64_t coro::io_service::process_events() {
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

std::uint64_t coro::io_service::process_pending_events() {
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

std::uint64_t coro::io_service::process_one_event() {
    std::uint64_t event_count = 0;
    if (try_enter_event_loop()) {
        auto exit_loop = on_scope_exit([&] { exit_event_loop(); });
        if (try_process_one_event(true)) {
            ++event_count;
        }
    }
    return event_count;
}

std::uint64_t coro::io_service::process_one_pending_event() {
    std::uint64_t event_count = 0;
    if (try_enter_event_loop()) {
        auto exit_loop = on_scope_exit([&] { exit_event_loop(); });
        if (try_process_one_event(false)) {
            ++event_count;
        }
    }
    return event_count;
}

void coro::io_service::stop() noexcept {
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

void coro::io_service::notify_work_finished() noexcept {
    if (m_work_count.fetch_sub(1, std::memory_order_relaxed) == 1) {
        stop();
    }
}

bool coro::io_service::try_process_one_event(const bool wait_for_event) {
        if (is_stop_requested()) {
            return false;
        }

        while (true) {
            try_reschedule_overflow_operations();
            detail::macos::io_message* message = nullptr;

            // 调用 dequeue 取出一个消息（或事件）
            bool status;
            try {
                status = m_uq.dequeue(message, wait_for_event);
            }
            catch (std::system_error& err) {
                if (err.code() == std::errc::interrupted &&
                    (m_thread_state.load(std::memory_order_relaxed) & stop_requested_flag) == 0) {
                    return false;
                }
                throw;
            }

            if (!status) {
                return false;
            }

            // 恢复协程事件
            if (message && message->handle) {
                message->handle.resume();
                return true;
            }

            if (is_stop_requested()) {
                return false;
            }
        }
    }

template<typename rep, typename period>
coro::io_service::timed_schedule_operation coro::io_service::schedule_after(
    const std::chrono::duration<rep, period>& delay, cancellation_token cancellation_token) noexcept {
    return timed_schedule_operation{
        *this,
        std::chrono::high_resolution_clock::now() + delay,
        std::move(cancellation_token)
    };
}

void coro::io_service::schedule_impl(schedule_operation* operation) noexcept {
    // 尝试将调度操作加入消息队列
    operation->m_message.handle = operation->m_awaiter;
    operation->m_message.type = detail::macos::RESUME_TYPE;
    bool ok = m_uq.transaction(operation->m_message).nop().commit();
    if (!ok) {
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

void coro::io_service::try_reschedule_overflow_operations() noexcept {
    auto* operation = m_schedule_operations.exchange(nullptr, std::memory_order_acquire);
    // 循环遍历溢出操作链表，尝试将每一个 schedule_operation 加入消息队列。
    while (operation) {
        auto* next = operation->m_next;
        operation->m_message.handle = operation->m_awaiter;
        operation->m_message.type = detail::macos::RESUME_TYPE;
        bool ok = m_uq.transaction(operation->m_message).nop().commit();
        if (!ok) {
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

void coro::io_service::schedule_operation::await_suspend(const std::coroutine_handle<> awaiter) noexcept {
    m_awaiter = awaiter;
    m_service.schedule_impl(this);
}

void coro::io_service::timed_schedule_operation::await_suspend(std::coroutine_handle<> awaiter) {
    m_schedule_operation.m_awaiter = awaiter;

    auto& service = m_schedule_operation.m_service;

    // 如果引用计数减少为1，表示可以调度任务了
    if (m_ref_count.fetch_sub(1, std::memory_order_acquire) == 1) {
        service.schedule_impl(&m_schedule_operation);
    }
}

