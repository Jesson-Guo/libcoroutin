//
// Created by Jesson on 2024/9/28.
//

#ifndef SINGLE_CONSUMER_EVENT_H
#define SINGLE_CONSUMER_EVENT_H

#include <atomic>
#include <coroutine>

namespace coro {

class single_consumer_event {
public:
    explicit single_consumer_event(const bool init=false) noexcept : m_state(init ? set_value : not_set_value) {}

    auto is_set() const noexcept -> bool {
        return m_state.load(std::memory_order_acquire) == set_value;
    }

    auto set() noexcept -> void {
        std::uintptr_t old_state = m_state.exchange(set_value, std::memory_order_acq_rel);

        // there was a coroutine awaiting, resume it
        if (old_state != set_value && old_state != not_set_value) {
            auto awaiter = reinterpret_cast<std::coroutine_handle<>>(old_state);
            awaiter.resume();
        }
    }

    auto reset() noexcept -> void {
        m_state.store(not_set_value, std::memory_order_relaxed);
    }

    auto operator co_await() const noexcept {
        class awaiter {
        public:
            explicit awaiter(single_consumer_event& event) noexcept : m_event(event) {}

            auto await_ready() const noexcept -> bool {
                return m_event.is_set();
            }

            auto await_suspend(std::coroutine_handle<> awaiter) const noexcept -> bool {
                if (std::uintptr_t old_state = not_set_value;
                    !m_event.m_state.compare_exchange_strong(
                    old_state,
                    reinterpret_cast<std::uintptr_t>(awaiter.address()),
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                    // already set, don't suspend
                    return false;
                }
                return true;
            }

            auto await_resume() const noexcept -> void {}

        private:
            single_consumer_event& m_event;
        };

        return awaiter{*this};
    }

private:
    static constexpr std::uintptr_t not_set_value = 0;
    static constexpr std::uintptr_t set_value = 1;

    std::atomic<std::uintptr_t> m_state;
};

}

#endif //SINGLE_CONSUMER_EVENT_H
