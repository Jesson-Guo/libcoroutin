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
	void reset() noexcept;

private:
	friend class async_auto_reset_event_operation;

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

inline async_auto_reset_event_operation async_auto_reset_event::operator co_await() const noexcept {
    auto old_state = m_state.load(std::memory_order_relaxed);
    auto set_count = static_cast<std::uint32_t>(old_state);
    auto waiter_count = static_cast<std::uint32_t>(old_state >> 32);
    if (set_count > waiter_count) {
        if (m_state.compare_exchange_strong(
                old_state, old_state - 1, std::memory_order_acquire, std::memory_order_relaxed)) {
            // set_count > waiter_count, won't suspend
            return async_auto_reset_event_operation{};
        }
    }
    return async_auto_reset_event_operation{*this};
}

inline void async_auto_reset_event::set() const noexcept {
    std::uint64_t old_state = m_state.fetch_add(1, std::memory_order_acq_rel);
    if (old_state >> 32 != 0) {
        resume_waiters(old_state + 1);
    }
}

inline void async_auto_reset_event::reset() noexcept {
    // reset the set count to zero
    std::uint64_t state     = m_state.load(std::memory_order_acquire);
    std::uint64_t new_state = state & 0xFFFFFFFF00000000ULL;
    m_state.store(new_state, std::memory_order_release);
}

inline void async_auto_reset_event::resume_waiters(std::uint64_t init_state) const noexcept {
    async_auto_reset_event_operation* resume_list = nullptr;
    async_auto_reset_event_operation** resume_list_end = nullptr;

    auto waiters_resume_count = std::min(
        static_cast<std::uint32_t>(init_state),
        static_cast<std::uint32_t>(init_state >> 32));

    assert(waiters_resume_count > 0);
    while (waiters_resume_count > 0) {
        for(auto i=0; i<waiters_resume_count; ++i) {
            if (!m_waiters) {
                auto* waiter = m_new_waiters.exchange(nullptr, std::memory_order_acquire);
                // reverse waiter order, fifo
                while (waiter) {
                    auto* next = waiter->m_next;
                    waiter->m_next = m_waiters;
                    m_waiters = waiter;
                    waiter = next;
                }
            }
            assert(m_waiters != nullptr);
            auto* waiter_to_resume = m_waiters;
            m_waiters = m_waiters->m_next;

            waiter_to_resume->m_next = nullptr;
            *resume_list_end = waiter_to_resume;
            resume_list_end = &waiter_to_resume->m_next;
        }

        // update set count and waiter count
        const std::uint64_t delta = static_cast<std::uint64_t>(waiters_resume_count) |
            static_cast<std::uint64_t>(waiters_resume_count) << 32;

        const std::uint64_t new_state = m_state.fetch_sub(delta, std::memory_order_acq_rel) - delta;

        waiters_resume_count = std::min(
            static_cast<std::uint32_t>(new_state),
            static_cast<std::uint32_t>(new_state >> 32));
    }

    assert(resume_list != nullptr);
    do {
        auto* const waiter = resume_list;

        // read 'next' before resuming since resuming the waiter is likely to destroy the waiter.
        auto* const next = resume_list->m_next;

        if (waiter->m_ref_count.fetch_sub(1, std::memory_order_release) == 1) {
            waiter->m_awaiter.resume();
        }

        resume_list = next;
    } while (resume_list);
}

inline auto async_auto_reset_event_operation::await_suspend(std::coroutine_handle<> awaiter) noexcept -> bool {
    m_awaiter = awaiter;
    // add to the m_newWaiters list.
    auto* waiter = m_event->m_new_waiters.load(std::memory_order_relaxed);
    while (true) {
        m_next = waiter;
        if (!m_event->m_new_waiters.compare_exchange_weak(
            waiter,
            this,
            std::memory_order_release,
            std::memory_order_relaxed)) {
            break;
        }
    }

    const std::uint64_t old_state = m_event->m_state.fetch_add(
        static_cast<std::uint64_t>(1) << 32, std::memory_order_acq_rel);

    if (old_state != 0 && static_cast<std::uint32_t>(old_state >> 32) == 0) {
        m_event->resume_waiters(old_state + static_cast<std::uint64_t>(1) << 32);
    }

    return m_ref_count.fetch_sub(1, std::memory_order_acquire) != 1;
}

}

#endif //ASYNC_AUTO_RESET_EVENT_H
