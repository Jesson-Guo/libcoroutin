//
// Created by Jesson on 2024/9/28.
//

#ifndef SINGLE_CONSUMER_EVENT_H
#define SINGLE_CONSUMER_EVENT_H

#include <atomic>
#include <coroutine>

namespace coro {

/// \brief
/// 支持单个等待协程的手动重置事件。
///
/// 你可以使用 `co_await` 该事件来挂起当前协程，直到某个线程调用 `set()`。
/// 如果事件已经被设置，协程不会挂起，并继续执行。
/// 如果事件尚未设置，那么协程会在 `set()` 被调用时由调用线程恢复。
///
/// 调用者必须确保在任何时间点，只有一个协程执行 `co_await` 语句。
class single_consumer_event {
public:
    explicit single_consumer_event(bool init = false) noexcept : m_state(init ? set_state : not_set_state) {}

    auto is_set() const noexcept -> bool {
        return m_state.load(std::memory_order_acquire) == set_state;
    }

    /// 如果事件尚未设置，则将其设置为 "设置" 状态。
    /// 如果有一个协程在等待事件，那么它会在调用 `set()` 的线程中被恢复。
    auto set() noexcept -> void {
        // 将状态原子性地交换为 "设置"，并获取旧状态。
        std::uintptr_t old_state = m_state.exchange(set_state, std::memory_order_acq_rel);

        if (old_state != not_set_state && old_state != set_state) {
            // 旧状态是一个协程句柄地址。
            auto awaiter = std::coroutine_handle<>::from_address(reinterpret_cast<void*>(old_state));
            awaiter.resume();
        }
    }

    auto reset() noexcept -> void {
        std::uintptr_t expected = set_state;
        m_state.compare_exchange_strong(expected, not_set_state, std::memory_order_relaxed);
    }

    auto operator co_await() noexcept {
        class awaiter {
        public:
            explicit awaiter(single_consumer_event& event) noexcept : m_event(event) {}
            bool await_ready() const noexcept { return m_event.is_set(); }
            bool await_suspend(std::coroutine_handle<> awaiter_handle) {
                auto expected = not_set_state;
                auto desired = reinterpret_cast<std::uintptr_t>(awaiter_handle.address());
                if (m_event.m_state.compare_exchange_strong(
                    expected,
                    desired,
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                    // 成功存储了协程句柄；挂起协程。
                    return true;
                }
                if (expected == set_state) {
                    // 事件已经设置，不挂起。
                    return false;
                }
                return false;
            }
            void await_resume() noexcept {}
        private:
            single_consumer_event& m_event;
        };

        return awaiter{ *this };
    }

private:
    // 状态的特殊值。
    static constexpr std::uintptr_t not_set_state = 0;
    static constexpr std::uintptr_t set_state = std::numeric_limits<std::uintptr_t>::max();

    // 原子状态变量，该变量可以包含以下值之一：
    // - not_set_state（0）
    // - set_state（最大 uintptr_t 值）
    // - 等待协程句柄的地址。
    std::atomic<std::uintptr_t> m_state;
};

}

#endif //SINGLE_CONSUMER_EVENT_H
