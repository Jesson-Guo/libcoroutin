//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/awaitable/async_auto_reset_event.h"

coro::async_auto_reset_event_operation coro::async_auto_reset_event::operator co_await() const noexcept {
    auto old_state = m_state.load(std::memory_order_relaxed);
    auto set_count = static_cast<std::uint32_t>(old_state);
    auto waiter_count = static_cast<std::uint32_t>(old_state >> 32);
    if (set_count > waiter_count) {
        if (m_state.compare_exchange_strong(
            old_state,
            old_state - 1,
            std::memory_order_acquire,
            std::memory_order_relaxed)) {
            // set_count > waiter_count, won't suspend
            return async_auto_reset_event_operation{};
        }
    }
    return async_auto_reset_event_operation{*this};
}

void coro::async_auto_reset_event::set() const noexcept {
    std::uint64_t old_state = m_state.load(std::memory_order_relaxed);
    do {
        auto set_count = static_cast<std::uint32_t>(old_state);
        auto waiter_count = static_cast<std::uint32_t>(old_state >> 32);
        if (set_count > waiter_count) {
            // already set
            return;
        }
    } while (!m_state.compare_exchange_weak(
        old_state,
        old_state + 1,
        std::memory_order_acq_rel,
        std::memory_order_acquire));

    if (old_state != 0 && static_cast<std::uint32_t>(old_state) == 0) {
        resume_waiters(old_state + 1);
    }
}

void coro::async_auto_reset_event::reset() const noexcept {
    // reset the set count to zero
    // std::uint64_t old_state = m_state.load(std::memory_order_acquire);
    // std::uint64_t new_state = old_state & 0xFFFFFFFF00000000ULL;
    // m_state.store(new_state, std::memory_order_release);
    // std::uint64_t oldState = m_state.load(std::memory_order_relaxed);

    std::uint64_t old_state = m_state.load(std::memory_order_relaxed);
    auto set_count = static_cast<std::uint32_t>(old_state);
    auto waiter_count = static_cast<std::uint32_t>(old_state >> 32);
    while (set_count > waiter_count) {
        if (m_state.compare_exchange_weak(
            old_state,
            old_state - 1,
            std::memory_order_relaxed)) {
            // successfully reset.
            return;
        }
    }
}

void coro::async_auto_reset_event::resume_waiters(std::uint64_t init_state) const noexcept {
    async_auto_reset_event_operation* resume_list = nullptr;
    async_auto_reset_event_operation** resume_list_end = &resume_list;

    auto waiters_resume_count = std::min(
        static_cast<std::uint32_t>(init_state),
        static_cast<std::uint32_t>(init_state >> 32));

    assert(waiters_resume_count > 0);
    do {
        for (auto i=0; i<waiters_resume_count; ++i) {
            if (!m_waiters) {
                auto* waiter = m_new_waiters.exchange(nullptr, std::memory_order_acquire);
                assert(waiter != nullptr);
                // reverse waiter order, fifo
                do {
                    auto* next = waiter->m_next;
                    waiter->m_next = m_waiters;
                    m_waiters = waiter;
                    waiter = next;
                } while (waiter);
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
    } while (waiters_resume_count > 0);

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

auto coro::async_auto_reset_event_operation::await_suspend(std::coroutine_handle<> awaiter) noexcept -> bool {
    m_awaiter = awaiter;
    // add to the m_newWaiters list.
    auto* waiter = m_event->m_new_waiters.load(std::memory_order_relaxed);
    do {
        m_next = waiter;
    } while (!m_event->m_new_waiters.compare_exchange_weak(
        waiter,
        this,
        std::memory_order_release,
        std::memory_order_relaxed));

    constexpr auto waiter_increment = static_cast<std::uint64_t>(1) << 32;
    const std::uint64_t old_state = m_event->m_state.fetch_add(waiter_increment, std::memory_order_acq_rel);

    if (old_state != 0 && static_cast<std::uint32_t>(old_state >> 32) == 0) {
        m_event->resume_waiters(old_state + waiter_increment);
    }

    return m_ref_count.fetch_sub(1, std::memory_order_acquire) != 1;
}
