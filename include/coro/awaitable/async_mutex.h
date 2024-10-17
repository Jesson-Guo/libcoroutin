//
// Created by Jesson on 2024/9/28.
//

#ifndef ASYNC_MUTEX_H
#define ASYNC_MUTEX_H

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

    /// \brief
    /// Attempt to acquire a lock on the mutex without blocking.
    ///
    /// \return
    /// true if the lock was acquired, false if the mutex was already locked.
    /// The caller is responsible for ensuring unlock() is called on the mutex
    /// to release the lock if the lock was acquired by this call.
    auto try_lock() noexcept -> bool {
        std::uintptr_t expected = not_locked;
        return m_state.compare_exchange_strong(
            expected,
            locked_no_waiters,
            std::memory_order_acquire,
            std::memory_order_relaxed);
    }

    /// \brief
    /// Acquire a lock on the mutex asynchronously.
    ///
    /// If the lock could not be acquired synchronously then the awaiting
    /// coroutine will be suspended and later resumed when the lock becomes
    /// available. If suspended, the coroutine will be resumed inside the
    /// call to unlock() from the previous lock owner.
    ///
    /// \return
    /// An operation object that must be 'co_await'ed to wait until the
    /// lock is acquired. The result of the 'co_await m.lock_async()'
    /// expression has type 'void'.
    auto lock_async() noexcept -> async_mutex_lock_operation;

    /// \brief
    /// Acquire a lock on the mutex asynchronously, returning an object that
    /// will call unlock() automatically when it goes out of scope.
    ///
    /// If the lock could not be acquired synchronously then the awaiting
    /// coroutine will be suspended and later resumed when the lock becomes
    /// available. If suspended, the coroutine will be resumed inside the
    /// call to unlock() from the previous lock owner.
    ///
    /// \return
    /// An operation object that must be 'co_await'ed to wait until the
    /// lock is acquired. The result of the 'co_await m.scoped_lock_async()'
    /// expression returns an 'async_mutex_lock' object that will call
    /// this->mutex() when it destructs.
    auto scoped_lock_async() noexcept -> async_mutex_scoped_lock_operation;

    /// \brief
    /// Unlock the mutex.
    ///
    /// Must only be called by the current lock-holder.
    ///
    /// If there are lock operations waiting to acquire the
    /// mutex then the next lock operation in the queue will
    /// be resumed inside this call.
    auto unlock() noexcept -> void;

private:
    friend class async_mutex_lock_operation;

    static constexpr std::uintptr_t not_locked = 1;
    static constexpr std::uintptr_t locked_no_waiters = 0;

    // This field provides synchronisation for the mutex.
    //
    // It can have three kinds of values:
    // - not_locked
    // - locked_no_waiters
    // - a pointer to the head of a singly linked list of recently
    //   queued async_mutex_lock_operation objects. This list is
    //   in most-recently-queued order as new items are pushed onto
    //   the front of the list.
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
    auto await_suspend(std::coroutine_handle<> awaiter) noexcept -> bool;
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
