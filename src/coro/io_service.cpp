//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/io/io_service.h"

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
    const auto old_state = m_thread_state.fetch_or(stop_requested_flag, std::memory_order_release);
    if ((old_state & stop_requested_flag) == 0) {
        // 唤醒所有正在等待事件的 I/O 线程，让它们及时退出事件循环。
        m_io_queue.stop();
    }
}

void coro::io_service::notify_work_finished() noexcept {
    if (m_work_count.fetch_sub(1, std::memory_order_relaxed) == 1) {
        stop();
    }
}

void coro::io_service::schedule_impl(schedule_operation* operation) noexcept {
    // 尝试将调度操作加入消息队列
    operation->m_message->handle = operation->m_awaiter;
    if (!m_io_queue.transaction(operation->m_message).nop().commit()) {
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
        operation->m_message->handle = operation->m_awaiter;
        bool ok = m_io_queue.transaction(operation->m_message).nop().commit();
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

bool coro::io_service::try_enter_event_loop() noexcept {
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

bool coro::io_service::try_process_one_event(bool wait_for_event) {
    if (is_stop_requested()) {
        return false;
    }

    try_reschedule_overflow_operations();
    detail::macos::io_message* message = nullptr;

    // 取出一个消息（或事件）
    if (!m_io_queue.dequeue(message, wait_for_event)) {
        return false;
    }

    // 恢复协程事件
    if (message && message->handle) {
        message->handle.resume();
    }

    if (is_stop_requested()) {
        return false;
    }
    return true;
}

void coro::io_service::schedule_operation::await_suspend(std::coroutine_handle<> awaiter) noexcept {
    m_awaiter = awaiter;
    m_service.schedule_impl(this);
}
