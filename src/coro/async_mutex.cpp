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
    // 检查当前锁是否被持有
    assert(m_state.load(std::memory_order_relaxed) != not_locked);

    // 取出等待者列表
    async_mutex_lock_operation* waiters_head = m_waiters;
    if (waiters_head == nullptr) {
        // 没有等待者，尝试将状态设置为 not_locked
        std::uintptr_t old_state = locked_no_waiters;
        if (m_state.compare_exchange_strong(
            old_state,
            not_locked,
            std::memory_order_release,
            std::memory_order_relaxed)) {
            // 解锁成功
            return;
        }

        // 有新的等待者，获取等待者列表
        old_state = m_state.exchange(locked_no_waiters, std::memory_order_acquire);

        assert(old_state != locked_no_waiters && old_state != not_locked);

        // 将等待者列表反转，准备恢复等待者
        auto* next = reinterpret_cast<async_mutex_lock_operation*>(old_state);
        do {
            auto* temp = next->m_next;
            next->m_next = waiters_head;
            waiters_head = next;
            next = temp;
        } while (next != nullptr);
    }

    assert(waiters_head != nullptr);

    m_waiters = waiters_head->m_next;

    // 恢复等待的协程，传递锁的所有权
    waiters_head->m_awaiter.resume();
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
                std::memory_order_acquire)) {
                // add to waiting list
                return true;
            }
        }
    }
}
