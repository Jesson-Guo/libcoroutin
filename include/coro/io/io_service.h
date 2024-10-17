#ifndef IO_SERVICE_H
#define IO_SERVICE_H

#include "../operation_cancelled.h"
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

namespace coro {

class io_service {
public:
    class schedule_operation;
    class timed_schedule_operation;

    explicit io_service()
        : m_thread_state(0)
        , m_work_count(0)
        , m_schedule_operations(nullptr) {}

    ~io_service() {
        assert(m_schedule_operations.load(std::memory_order_relaxed) == nullptr);
        assert(m_thread_state.load(std::memory_order_relaxed) < active_thread_count_increment);
    }

    io_service(io_service&& other) = delete;
    io_service(const io_service& other) = delete;
    io_service& operator=(io_service&& other) = delete;
    io_service& operator=(const io_service& other) = delete;

    /// 返回一个操作，当被等待时，将挂起等待的协程，并将其重新调度到与此 io_service 关联的 I/O 线程上。
    [[nodiscard]] schedule_operation schedule() noexcept;

    /// 返回一个操作，当被等待时，将挂起等待的协程指定的延迟时间。
    /// 一旦延迟时间过去，协程将在与此 io_service 关联的 I/O 线程上恢复执行。
    template<typename rep, typename period>
    [[nodiscard]] timed_schedule_operation schedule_after(const std::chrono::duration<rep, period>& delay, cancellation_token ct = {}) noexcept;

    /// 处理事件直到 io_service 被停止。
    std::uint64_t process_events();

    /// 处理事件直到 io_service 被停止或队列中没有更多的待处理事件。
    std::uint64_t process_pending_events();

    /// 阻塞直到处理一个事件或 io_service 被停止。
    std::uint64_t process_one_event();

    /// 如果有任何事件待处理，则处理一个事件，否则如果没有待处理事件或 io_service 被停止，则立即返回。
    std::uint64_t process_one_pending_event();

    /// 关闭 io_service
    void stop() noexcept;

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
    void notify_work_finished() noexcept;

    detail::macos::io_queue& io_queue() noexcept { return m_uq; }

private:
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
    /// true:  表示调用 dequeue 时，如果队列中没有事件，应该阻塞等待直到有事件到达。
    /// false: 表示 dequeue 立即返回，如果没有事件可处理则返回 false。
    bool try_process_one_event(bool wait_for_event);

    /// 唤醒可能处于阻塞状态的 I/O 线程，使其能够及时地重新进入事件循环，处理队列中的待处理任务或溢出的操作。
    void post_wake_up_event() noexcept {
        // 向消息队列中加入一个特殊的唤醒事件，唤醒阻塞在 kevent 上的线程。
        static detail::macos::io_message nop;
        m_uq.transaction(nop).nop().commit();
    }

    static constexpr std::uint32_t stop_requested_flag = 1;
    static constexpr std::uint32_t active_thread_count_increment = 2;

    // 位 0: stop_requested_flag
    // 位 1-31: 当前正在运行事件循环的活动线程数
    std::atomic<std::uint32_t> m_thread_state;
    std::atomic<std::uint32_t> m_work_count;

    detail::macos::io_queue m_uq;
    detail::macos::io_message m_nopMessage{};

    // 准备运行但未能加入消息队列的调度操作的链表头
    std::atomic<schedule_operation*> m_schedule_operations;
};

class io_service::schedule_operation {
public:
    explicit schedule_operation(io_service& service) noexcept
        : m_service(service)
        , m_next(nullptr) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> awaiter) noexcept;

    void await_resume() const noexcept {}

private:
    friend class io_service;
    friend class timed_schedule_operation;

    io_service& m_service;
    std::coroutine_handle<> m_awaiter;
    schedule_operation* m_next;
    detail::macos::io_message m_message{};
};

class io_service::timed_schedule_operation {
public:
    using time_point = std::chrono::high_resolution_clock::time_point;

    timed_schedule_operation(io_service& service, const time_point resume_time, cancellation_token token) noexcept
        : m_schedule_operation(service)
        , m_resume_time(resume_time)
        , m_cancellation_token(std::move(token))
        , m_next(nullptr)
        , m_ref_count(2) {
        m_cancellation_registration.emplace(std::move(m_cancellation_token), [&service, this] {
            service.io_queue().transaction(m_message).timeout_remove().commit();
        });
    }

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
        if (m_message.result == -ETIME) {}
        else if (m_message.result == -ECANCELED) {
            throw operation_cancelled{};
        }
        else if (m_message.result < 0) {
            throw std::system_error {
                -m_message.result,
                std::generic_category()
            };
        }
    }

private:
    friend class timer_queue;

    schedule_operation m_schedule_operation;
    time_point m_resume_time;
    cancellation_token m_cancellation_token;
    std::optional<cancellation_registration> m_cancellation_registration;
    timed_schedule_operation* m_next;
    std::atomic<std::uint32_t> m_ref_count;
    detail::macos::io_message m_message{};
};

}

#endif // IO_SERVICE_H
