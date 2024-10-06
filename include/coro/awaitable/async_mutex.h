//
// Created by Jesson on 2024/9/28.
//

#ifndef ASYNC_MUTEX_H
#define ASYNC_MUTEX_H

#include "../async_generator.h"


#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstdint>
#include <mutex>

namespace coro {

class async_mutex_lock;
class async_mutex_lock_operation;
class async_mutex_scoped_lock_operation;

// using co_await to lock asynchronously
class async_mutex {
public:
    async_mutex() noexcept
        : m_state(not_locked)
        , m_waiters(nullptr) {}

    ~async_mutex() noexcept {
        const auto old_state = m_state.load(std::memory_order_relaxed);
        assert(old_state == not_locked || old_state == locked_no_waiters);
        assert(m_waiters == nullptr);
    }

    auto try_lock() noexcept -> bool {
        std::uintptr_t expected = not_locked;
        return m_state.compare_exchange_strong(
            expected,
            locked_no_waiters,
            std::memory_order_acquire,
            std::memory_order_relaxed);
    }

    auto lock_async() noexcept -> async_mutex_lock_operation {
        return async_mutex_lock_operation{*this};
    }

    auto scoped_lock_async() noexcept -> async_mutex_scoped_lock_operation {
        return async_mutex_scoped_lock_operation{*this};
    }

    auto unlock() noexcept -> void {
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

private:
    friend class async_mutex_lock_operation;

    static constexpr std::uintptr_t not_locked = 1;
    static constexpr std::uintptr_t locked_no_waiters = 0;

    std::atomic<std::uintptr_t> m_state;
    async_mutex_lock_operation* m_waiters;
};

class async_mutex_lock {
public:
    explicit async_mutex_lock(async_mutex& mutex, std::adopt_lock_t) noexcept : m_mutex(&mutex) {}

    async_mutex_lock(async_mutex_lock&& other) noexcept : m_mutex(other.m_mutex) {
        other.m_mutex = nullptr;
    }

    async_mutex_lock(const async_mutex_lock&) = delete;
    async_mutex_lock& operator=(const async_mutex_lock&) = delete;

    ~async_mutex_lock() noexcept {
        if (m_mutex) {
            m_mutex->unlock();
        }
    }

private:
    async_mutex* m_mutex;
};

class async_mutex_lock_operation {
public:
    explicit async_mutex_lock_operation(async_mutex& mutex) noexcept : m_mutex(mutex) {}

    auto await_ready() noexcept -> bool { return false; }

    auto await_suspend(std::coroutine_handle<> awaiter) noexcept -> bool {
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

    auto await_resume() const noexcept -> void {}

protected:
    friend class async_mutex;
    async_mutex& m_mutex;

private:
    async_mutex_lock_operation* m_next;
    std::coroutine_handle<> m_awaiter;
};

class async_mutex_scoped_lock_operation : public async_mutex_lock_operation {
public:
    using async_mutex_lock_operation::async_mutex_lock_operation;

    auto await_resume() const noexcept -> async_mutex_lock {
        return async_mutex_lock{m_mutex, std::adopt_lock};
    }
};

}

#endif //ASYNC_MUTEX_H
