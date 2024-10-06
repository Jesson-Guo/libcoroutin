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

    auto is_set() noexcept -> bool {
        return m_state.load(std::memory_order_acquire) == static_cast<void*>(this);
    }

    auto set() noexcept -> void {
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

    auto reset() noexcept -> void {
        void* expected = this;
        m_state.compare_exchange_strong(expected, this, std::memory_order_acq_rel);
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
        : m_event(event) {}

    auto await_ready() const noexcept -> bool {
        return m_event.is_set();
    }

    auto await_suspend(std::coroutine_handle<> awaiter) noexcept -> bool {
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

    auto await_resume() const noexcept -> void {}

private:
    friend class async_manual_reset_event;

    const async_manual_reset_event& m_event;
    async_manual_reset_event_operation* m_next;
    std::coroutine_handle<> m_awaiter;
};

inline async_manual_reset_event_operation async_manual_reset_event::operator co_await() const noexcept {
    return async_manual_reset_event_operation{ *this };
}

}

#endif //ASYNC_MANUAL_RESET_EVENT_H
