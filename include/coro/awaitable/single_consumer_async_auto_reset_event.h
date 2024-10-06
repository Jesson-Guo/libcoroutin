//
// Created by Jesson on 2024/9/29.
//

#ifndef SINGLE_CONSUMER_ASYNC_AUTO_RESET_EVENT_H
#define SINGLE_CONSUMER_ASYNC_AUTO_RESET_EVENT_H

#include <coroutine>
#include <atomic>
#include <cassert>

namespace coro {

class single_consumer_async_auto_reset_event {
public:
    explicit single_consumer_async_auto_reset_event(const bool init=false) noexcept
        : m_state(init ? this : nullptr) {}

    auto set() noexcept -> void {
        void* old_state = m_state.load(std::memory_order_release);
        if (old_state != nullptr && old_state != this) {
            // resume one waiting coroutine
            auto handle = *static_cast<std::coroutine_handle<>>(old_state);
            (void)m_state.exchange(nullptr, std::memory_order_acquire);
            handle.resume();
        }
    }

    auto operator co_await() const noexcept {
        class awaiter {
        public:
            awaiter(const single_consumer_async_auto_reset_event& event) noexcept
                : m_event(event) {}

            auto await_ready() noexcept -> bool { return false; }

            auto await_suspend(std::coroutine_handle<> awaiting_handle) noexcept -> bool {
                m_awaiting_handle = awaiting_handle;
                void* old_state = nullptr;
                if (!m_event.m_state.compare_exchange_strong(
                    old_state,
                    &m_awaiting_handle,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {
                    assert(old_state == &m_event);
                    (void)m_event.m_state.exchange(nullptr, std::memory_order_acquire);
                    return false;
                }
                return true;
            }

            auto await_resume() noexcept -> void {}

        private:
            const single_consumer_async_auto_reset_event& m_event;
            std::coroutine_handle<> m_awaiting_handle;
        };

        return awaiter{ *this };
    }

private:
    // nullptr - not set, no waiter
    // this    - set
    // other   - not set, pointer is address of a coroutine_handle<> to resume.
    mutable std::atomic<void*> m_state;
};

}

#endif //SINGLE_CONSUMER_ASYNC_AUTO_RESET_EVENT_H
