//
// Created by Jesson on 2024/9/30.
//

#ifndef ASYNC_LATCH_H
#define ASYNC_LATCH_H

#include "async_manual_reset_event.h"
#include <atomic>

namespace coro {

class async_latch {
public:
    /// Construct the latch with the specified initial count.
    ///
    /// \param init_count
    /// The initial count of the latch. The latch will become signalled once
    /// \c this->count_down() has been called \p initialCount times.
    /// The latch will be immediately signalled on construction if this
    /// parameter is zero or negative.
    explicit async_latch(std::ptrdiff_t init_count) noexcept
        : m_count(init_count)
        , m_event(init_count <= 0) {}

    /// Query if the latch has become signalled.
    ///
    /// The latch is marked as signalled once the count reaches zero.
    bool is_ready() const noexcept {
        return m_event.is_set();
    }

    /// Decrement the count by n.
    ///
    /// Any coroutines awaiting this latch will be resumed once the count
    /// reaches zero. ie. when this method has been called at least 'initialCount'
    /// times.
    ///
    /// Any awaiting coroutines that are currently suspended waiting for the
    /// latch to become signalled will be resumed inside the last call to this
    /// method (ie. the call that decrements the count to zero).
    ///
    /// \param n
    /// The amount to decrement the count by.
    void count_down(std::ptrdiff_t n = 1) noexcept {
        if (m_count.fetch_sub(n, std::memory_order_acq_rel) <= n) {
            m_event.set();
        }
    }

    /// Allows the latch to be awaited within a coroutine.
    ///
    /// If the latch is already signalled (ie. the count has been decremented
    /// to zero) then the awaiting coroutine will continue without suspending.
    /// Otherwise, the coroutine will suspend and will later be resumed inside
    /// a call to `count_down()`.
    auto operator co_await() const noexcept {
        return m_event.operator co_await();
    }

private:
    std::atomic<std::ptrdiff_t> m_count;
    async_manual_reset_event m_event;
};

}

#endif //ASYNC_LATCH_H
