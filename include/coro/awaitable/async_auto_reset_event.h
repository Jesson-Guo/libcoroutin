//
// Created by Jesson on 2024/9/29.
//

#ifndef ASYNC_AUTO_RESET_EVENT_H
#define ASYNC_AUTO_RESET_EVENT_H

#include <coroutine>
#include <atomic>
#include <cassert>
#include <algorithm>

namespace coro {

class async_auto_reset_event_operation;

class async_auto_reset_event {
public:
	explicit async_auto_reset_event(const bool init=false) noexcept
        : m_state(init ? 1 : 0)
        , m_new_waiters(nullptr)
        , m_waiters(nullptr) {}

	~async_auto_reset_event() {
	    assert(m_state.load(std::memory_order_relaxed) >> 32 == 0);
	    assert(m_new_waiters.load(std::memory_order_relaxed) == nullptr);
	    assert(m_waiters == nullptr);
	}

	async_auto_reset_event_operation operator co_await() const noexcept;
	void set() const noexcept;
	void reset() const noexcept;

private:
	friend class async_auto_reset_event_operation;

    // TODO 使用批处理方式恢复多个等待者，减少原子操作的频繁调用。
	void resume_waiters(std::uint64_t init_state) const noexcept;

	// Bits 0-31  - set count
	// Bits 32-63 - waiter count
	mutable std::atomic<std::uint64_t> m_state;
	mutable std::atomic<async_auto_reset_event_operation*> m_new_waiters;
	mutable async_auto_reset_event_operation* m_waiters;
};

class async_auto_reset_event_operation {
public:
    async_auto_reset_event_operation() noexcept
        : m_event(nullptr)
        , m_next(nullptr)
        , m_awaiter(nullptr)
        , m_ref_count(0) {}

    explicit async_auto_reset_event_operation(const async_auto_reset_event& event) noexcept
        : m_event(&event)
        , m_next(nullptr)
        , m_awaiter(nullptr)
        , m_ref_count(2) {}

    async_auto_reset_event_operation(const async_auto_reset_event_operation& other) noexcept
        : m_event(other.m_event)
        , m_next(nullptr)
        , m_awaiter(nullptr)
        , m_ref_count(2) {}

    auto await_ready() const noexcept -> bool { return m_event == nullptr; }
    auto await_suspend(std::coroutine_handle<> awaiter) noexcept -> bool;
    auto await_resume() const noexcept -> void {}

private:
    friend class async_auto_reset_event;

    const async_auto_reset_event* m_event;
    async_auto_reset_event_operation* m_next;
    std::coroutine_handle<> m_awaiter;
    std::atomic<std::uint32_t> m_ref_count;
};

}

#endif //ASYNC_AUTO_RESET_EVENT_H
