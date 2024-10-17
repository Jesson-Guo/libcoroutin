//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/awaitable/async_manual_reset_event.h"

coro::async_manual_reset_event_operation coro::async_manual_reset_event::operator co_await() const noexcept {
    return async_manual_reset_event_operation{ *this };
}

auto coro::async_manual_reset_event::set() noexcept -> void {
    void* const old_state = m_state.exchange(this, std::memory_order_acq_rel);
    if (old_state != static_cast<void*>(this)) {
        auto* waiter = static_cast<async_manual_reset_event_operation*>(old_state);
        while (waiter) {
            auto* next = waiter->m_next;
            waiter->m_awaiter.resume();
            waiter = next;
        }
    }
}

auto coro::async_manual_reset_event_operation::await_ready() const noexcept -> bool {
    return m_event.is_set();
}

auto coro::async_manual_reset_event_operation::await_suspend(std::coroutine_handle<> awaiter) noexcept -> bool {
    m_awaiter = awaiter;
    void* old_state = m_event.m_state.load(std::memory_order_acquire);

    while (true) {
        if (old_state != &m_event) {
            // event is set, no need suspending.
            return false;
        }
        // add to waiting list
        m_next = static_cast<async_manual_reset_event_operation*>(old_state);
        if (m_event.m_state.compare_exchange_weak(
            old_state,
            this,
            std::memory_order_release,
            std::memory_order_acquire)) {
            return true;
        }
    }
}
