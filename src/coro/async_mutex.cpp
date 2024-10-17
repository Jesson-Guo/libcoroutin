//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/awaitable/async_mutex.h"

auto coro::async_mutex::lock_async() noexcept -> async_mutex_lock_operation {
    return async_mutex_lock_operation{*this};
}

auto coro::async_mutex::scoped_lock_async() noexcept -> async_mutex_scoped_lock_operation {
    return async_mutex_scoped_lock_operation{*this};
}

auto coro::async_mutex::unlock() noexcept -> void {
    std::uintptr_t old_state = m_state.load(std::memory_order_acquire);
    while (true) {
        if (old_state != locked_no_waiters) {
            if (m_state.compare_exchange_weak(
                old_state,
                not_locked,
                std::memory_order_release,
                std::memory_order_relaxed)) {
                // unlocked successfully
                return;
            }
        }
        else if (old_state == not_locked) {
            // already unlocked
            assert(false && "Unlocked called on an unlocked mutex");
            return;
        }
        else {
            auto* waiter = reinterpret_cast<async_mutex_lock_operation*>(old_state);
            auto* next_waiter = waiter->m_next;

            std::uintptr_t new_state = next_waiter != nullptr ?
                reinterpret_cast<std::uintptr_t>(next_waiter) : locked_no_waiters;

            if (m_state.compare_exchange_weak(
                old_state,
                new_state,
                std::memory_order_release,
                std::memory_order_relaxed)) {
                // Successfully transferred lock to waiter
                waiter->m_awaiter.resume();
                return;
            }
        }
    }
}

auto coro::async_mutex_lock_operation::await_suspend(std::coroutine_handle<> awaiter) noexcept -> bool {
    m_awaiter = awaiter;
    std::uintptr_t old_state = m_mutex.m_state.load(std::memory_order_acquire);
    while (true) {
        if (old_state == async_mutex::not_locked) {
            if (m_mutex.m_state.compare_exchange_weak(
                old_state,
                async_mutex::locked_no_waiters,
                std::memory_order_acquire,
                std::memory_order_relaxed)) {
                // acquire lock successfully, no suspend.
                return false;
            }
        }
        else {
            m_next = reinterpret_cast<async_mutex_lock_operation*>(old_state);
            if (m_mutex.m_state.compare_exchange_weak(
                old_state,
                reinterpret_cast<std::uintptr_t>(this),
                std::memory_order_release,
                std::memory_order_relaxed)) {
                // add to waiting list
                return true;
            }
        }
    }
}
