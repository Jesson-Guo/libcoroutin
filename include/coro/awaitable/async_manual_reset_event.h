//
// Created by Jesson on 2024/9/29.
//

#ifndef ASYNC_MANUAL_RESET_EVENT_H
#define ASYNC_MANUAL_RESET_EVENT_H

#include <coroutine>
#include <atomic>
#include <cassert>

namespace coro {

class async_manual_reset_event_operation;

class async_manual_reset_event {
public:
    explicit async_manual_reset_event(const bool init=false) noexcept
        : m_state(init ? static_cast<void*>(this) : nullptr) {}

    ~async_manual_reset_event() {
        const auto old_state = m_state.load(std::memory_order_relaxed);
        assert(old_state == nullptr || old_state == static_cast<void*>(this));
    }

    async_manual_reset_event_operation operator co_await() const noexcept;

    auto is_set() const noexcept -> bool {
        auto a = m_state.load(std::memory_order_acquire) == static_cast<const void*>(this);
        return m_state.load(std::memory_order_acquire) == static_cast<const void*>(this);
    }

    auto set() noexcept -> void;

    auto reset() noexcept -> void {
        void* expected = this;
        m_state.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }

private:
    friend class async_manual_reset_event_operation;

    // This variable has 3 states:
    // - this    - The state is 'set'.
    // - nullptr - The state is 'not set' with no waiters.
    // - other   - The state is 'not set'.
    //             Points to an 'async_manual_reset_event_operation' that is
    //             the head of a linked-list of waiters.
    mutable std::atomic<void*> m_state;
};

class async_manual_reset_event_operation {
public:
    explicit async_manual_reset_event_operation(const async_manual_reset_event& event) noexcept
        : m_event(event)
        , m_next(nullptr) {}

    auto await_ready() const noexcept -> bool;
    auto await_suspend(std::coroutine_handle<> awaiter) noexcept -> bool;
    auto await_resume() const noexcept -> void {}

private:
    friend class async_manual_reset_event;

    const async_manual_reset_event& m_event;
    async_manual_reset_event_operation* m_next;
    std::coroutine_handle<> m_awaiter;
};

}

#endif //ASYNC_MANUAL_RESET_EVENT_H
