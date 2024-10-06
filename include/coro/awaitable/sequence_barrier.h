//
// Created by Jesson on 2024/10/1.
//

#ifndef SEQUENCE_BARRIER_H
#define SEQUENCE_BARRIER_H

#include "../detail/get_awaiter.h"
#include "../awaitable_traits.h"
#include "../sequence_traits.h"
#include "../detail/manual_lifetime.h"

#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstdint>
#include <limits>
#include <optional>

namespace coro {

template<typename seq, typename traits>
class sequence_barrier_wait_operation_base;

template<typename seq, typename traits, typename scheduler>
class sequence_barrier_wait_operation;

template<typename seq=std::size_t, typename traits=sequence_traits<seq>>
class sequence_barrier {
public:
    using awaiter_t = sequence_barrier_wait_operation_base<seq, traits>;

    explicit sequence_barrier(seq init_seq=traits::initial_sequence) noexcept
        : m_last_published(init_seq)
        , m_awaiters(nullptr) {}

    ~sequence_barrier() noexcept {
        // deconstruct before all waiters resumed
        assert(m_awaiters.load(std::memory_order_relaxed) == nullptr);
    }

    seq last_published() const noexcept {
        return m_last_published.load(std::memory_order_acquire);
    }

    template<typename scheduler>
    auto wait_until_published(seq target_seq, scheduler& sdl) const noexcept -> sequence_barrier_wait_operation<seq, traits, scheduler>;

    auto publish(seq sequence) noexcept -> void;

private:
    friend class sequence_barrier_wait_operation_base<seq, traits>;

    auto add_awaiter(awaiter_t* awaiter) const noexcept;

    // CPU cache_line is 64
    // First cache-line is written to by the producer only
    alignas(64) std::atomic<seq> m_last_published;
    alignas(64) std::atomic<awaiter_t*> m_awaiters;
};

template<typename seq, typename traits=sequence_traits<seq>>
class sequence_barrier_wait_operation_base {
public:
    explicit sequence_barrier_wait_operation_base(sequence_barrier<seq, traits>& barrier, seq target_seq) noexcept
        : m_barrier(barrier)
        , m_target_seq(target_seq)
        , m_last_published_seq(barrier.last_published())
        , m_next(nullptr)
        , m_ready_to_resume(false) {}

    sequence_barrier_wait_operation_base(const sequence_barrier_wait_operation_base& other) noexcept
        : m_barrier(other.m_barrier)
        , m_target_seq(other.m_target_seq)
        , m_last_published_seq(other.m_last_published_seq)
        , m_next(nullptr)
        , m_ready_to_resume(false) {}

    auto await_ready() const noexcept -> bool {
        return !traits::precedes(m_last_published_seq, m_target_seq);
    }

    auto await_suspend(std::coroutine_handle<> awaiting_handle) noexcept -> bool {
        m_awaiting_handle = awaiting_handle;
        m_barrier.add_awaiter(awaiting_handle);
        return !m_ready_to_resume.exchange(true, std::memory_order_acquire);
    }

    auto await_resume() const noexcept -> seq {
        return m_last_published_seq;
    }

protected:
    friend class sequence_barrier<seq, traits>;

    auto resume() noexcept -> void {
        if (m_ready_to_resume.exchange(true, std::memory_order_release)) {
            resume_impl();
        }
    }

    virtual void resume_impl() noexcept = 0;

    const sequence_barrier<seq, traits>& m_barrier;
    const seq m_target_seq;
    seq m_last_published_seq;
    sequence_barrier_wait_operation_base* m_next;
    std::coroutine_handle<> m_awaiting_handle;
    std::atomic<bool> m_ready_to_resume;
};

template<typename seq, typename traits, typename scheduler>
class sequence_barrier_wait_operation : public sequence_barrier_wait_operation_base<seq, traits> {
public:
    using schedule_operation = decltype(std::declval<scheduler&>().schedule());

    sequence_barrier_wait_operation(const sequence_barrier<seq, traits>& barrier, seq target_seq, scheduler& sdl) noexcept
        : sequence_barrier_wait_operation_base<seq, traits>(barrier, target_seq)
        , m_scheduler(sdl) {}

    sequence_barrier_wait_operation(const sequence_barrier_wait_operation& other) noexcept
        : sequence_barrier_wait_operation_base<seq, traits>(other)
        , m_scheduler(other.m_scheduler) {}

    ~sequence_barrier_wait_operation() noexcept {
        if (m_schedule_awaiter_created) {
            m_schedule_awaiter.destruct();
        }
        if (m_schedule_operation_created) {
            m_schedule_operation.destruct();
        }
    }

    auto await_resume() const noexcept(noexcept(m_schedule_awaiter->await_resume())) -> decltype(auto) {
        if (m_schedule_awaiter_created) {
            m_schedule_awaiter->await_resume();
        }

        return sequence_barrier_wait_operation_base<seq, traits>::await_resume();
    }

private:
    auto resume_impl() noexcept -> void {
        m_schedule_operation.construct(m_scheduler.schedule());
        m_schedule_operation_created = true;

        m_schedule_awaiter.construct(detail::get_awaiter(static_cast<schedule_operation&&>(*m_schedule_operation)));
        m_schedule_awaiter_created = true;

        if (!m_schedule_awaiter->await_ready()) {
            using await_suspend_result_t = decltype(m_schedule_awaiter->await_suspend(this->m_awaiting_handle));
            if constexpr (std::is_void_v<await_suspend_result_t>) {
                m_schedule_awaiter->await_suspend(this->m_awaiting_handle);
                return;
            }
            else if constexpr (std::is_same_v<await_suspend_result_t, bool>) {
                if (m_schedule_awaiter->await_suspend(this->m_awaiting_handle)) {
                    return;
                }
            }
            else {
                m_schedule_awaiter->await_suspend(this->m_awaiting_handle).resume();
                return;
            }
        }
        this->m_awaiting_handle.resume();
    }

    scheduler& m_scheduler;
    detail::manual_lifetime<schedule_operation> m_schedule_operation;
    detail::manual_lifetime<typename detail::awaitable_traits<schedule_operation>::awaiter_t> m_schedule_awaiter;
    bool m_schedule_operation_created = false;
    bool m_schedule_awaiter_created = false;
};

template<typename seq, typename traits>
template<typename scheduler>
auto sequence_barrier<seq, traits>::wait_until_published(seq target_seq, scheduler& sdl) const noexcept
    -> sequence_barrier_wait_operation<seq, traits, scheduler> {
    return sequence_barrier_wait_operation<seq, traits, scheduler>(*this, target_seq, sdl);
}

template<typename seq, typename traits>
auto sequence_barrier<seq, traits>::publish(seq sequence) noexcept -> void {

}


}

#endif //SEQUENCE_BARRIER_H
