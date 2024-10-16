//
// Created by Jesson on 2024/10/16.
//

#ifndef sequence_BARRIER_H
#define sequence_BARRIER_H

#include "awaitable_traits.h"
#include "sequence_traits.h"
#include "detail/manual_lifetime.h"

#include <atomic>
#include <cassert>
#include <limits>
#include <optional>
#include <coroutine>

namespace coro {

template<typename sequence, typename traits>
class sequence_barrier_wait_operation_base;

template<typename sequence, typename traits, typename scheduler>
class sequence_barrier_wait_operation;

template<typename sequence = std::size_t, typename traits = sequence_traits<sequence>>
class sequence_barrier {
	static_assert(std::is_integral_v<sequence>, "sequence_barrier requires an integral sequence type");
	using awaiter_t = sequence_barrier_wait_operation_base<sequence, traits>;

public:
	/// Construct a sequence barrier with the specified initial sequence number
	/// as the initial value 'last_published()'.
	explicit sequence_barrier(sequence init = traits::initial_sequence) noexcept
		: m_last_published(init)
		, m_awaiters(nullptr) {}

	~sequence_barrier() {
		// Shouldn't be destructing a sequence barrier if there are still waiters.
		assert(m_awaiters.load(std::memory_order_relaxed) == nullptr);
	}

	/// Query the sequence number that was most recently published by the producer.
	///
	/// You can assume that all sequence numbers prior to the returned sequence number
	/// have also been published. This means you can safely access all elements with
	/// sequence numbers up to and including the returned sequence number without any
	/// further synchronisation.
	sequence last_published() const noexcept {
		return m_last_published.load(std::memory_order_acquire);
	}

	/// Wait until a particular sequence number has been published.
	///
	/// If the specified sequence number is not yet published then the awaiting coroutine
	/// will be suspended and later resumed inside the call to publish() that publishes
	/// the specified sequence number.
	///
	/// \param target_sequence
	/// The sequence number to wait for.
	///
	/// \param sched
	/// scheduler
	///
	/// \return
	/// An awaitable that when co_await'ed will suspend the awaiting coroutine until
	/// the specified target sequence number has been published.
	/// The result of the co_await expression will be the last-known published sequence
	/// number. This is guaranteed not to precede \p targetsequence but may be a sequence
	/// number after \p targetsequence, which indicates that more elements have been
	/// published than you were waiting for.
	template<typename scheduler>
	[[nodiscard]] sequence_barrier_wait_operation<sequence, traits, scheduler> wait_until_published(
		sequence target_sequence, scheduler& sched) const noexcept;

	/// Publish the specified sequence number to consumers.
	///
	/// This publishes all sequence numbers up to and including the specified sequence
	/// number. This will resume any coroutine that was suspended waiting for a sequence
	/// number that was published by this operation.
	///
	/// \param seq
	/// The sequence number to publish. This number must not precede the current
	/// last_published() value. ie. the published sequence numbers must be monotonically
	/// increasing.
	void publish(sequence seq) noexcept;

private:
	friend class sequence_barrier_wait_operation_base<sequence, traits>;

	void add_awaiter(awaiter_t* awaiter) const noexcept;

	// First cache-line is written to by the producer only
	alignas(64) std::atomic<sequence> m_last_published;

	// Second cache-line is written to by both the producer and consumers
	alignas(64) mutable std::atomic<awaiter_t*> m_awaiters;
};

template<typename sequence, typename traits>
class sequence_barrier_wait_operation_base {
public:
    virtual ~sequence_barrier_wait_operation_base() = default;

    explicit sequence_barrier_wait_operation_base(const sequence_barrier<sequence, traits>& barrier, sequence target_sequence) noexcept
        : m_barrier(barrier)
        , m_target_sequence(target_sequence)
        , m_last_known_published(barrier.last_published())
        , m_next(nullptr)
        , m_ready_to_resume(false) {}

    sequence_barrier_wait_operation_base(const sequence_barrier_wait_operation_base& other) noexcept
        : m_barrier(other.m_barrier)
        , m_target_sequence(other.m_target_sequence)
        , m_last_known_published(other.m_last_known_published)
        , m_next(nullptr)
        , m_ready_to_resume(false) {}

    bool await_ready() const noexcept {
        return !traits::precedes(m_last_known_published, m_target_sequence);
    }

    bool await_suspend(std::coroutine_handle<> awaiting_handle) noexcept {
        m_awaiting_handle = awaiting_handle;
        m_barrier.add_awaiter(this);
        return !m_ready_to_resume.exchange(true, std::memory_order_acquire);
    }

    sequence await_resume() noexcept {
        return m_last_known_published;
    }

protected:
    friend class sequence_barrier<sequence, traits>;

    void resume() noexcept {
        // This synchronises with the exchange(true, std::memory_order_acquire) in await_suspend().
        if (m_ready_to_resume.exchange(true, std::memory_order_release)) {
            resume_impl();
        }
    }

    virtual void resume_impl() noexcept = 0;

    const sequence_barrier<sequence, traits>& m_barrier;
    const sequence m_target_sequence;
    sequence m_last_known_published;
    sequence_barrier_wait_operation_base* m_next;
    std::coroutine_handle<> m_awaiting_handle;
    std::atomic<bool> m_ready_to_resume;
};

template<typename sequence, typename traits, typename scheduler>
class sequence_barrier_wait_operation : public sequence_barrier_wait_operation_base<sequence, traits> {
	using schedule_operation = decltype(std::declval<scheduler&>().schedule());

public:
	sequence_barrier_wait_operation(
		const sequence_barrier<sequence, traits>& barrier, sequence target_sequence, scheduler& sched) noexcept
		: sequence_barrier_wait_operation_base<sequence, traits>(barrier, target_sequence)
		, m_scheduler(sched) {}

	sequence_barrier_wait_operation(const sequence_barrier_wait_operation& other) noexcept
		: sequence_barrier_wait_operation_base<sequence, traits>(other)
		, m_scheduler(other.m_scheduler) {}

	~sequence_barrier_wait_operation() override {
		if (m_is_schedule_awaiter_created) {
			m_schedule_awaiter.destruct();
		}
		if (m_is_schedule_operation_created) {
			m_schedule_operation.destruct();
		}
	}

	decltype(auto) await_resume() noexcept(noexcept(m_schedule_awaiter->await_resume())) {
		if (m_is_schedule_awaiter_created) {
			m_schedule_awaiter->await_resume();
		}
		return sequence_barrier_wait_operation_base<sequence, traits>::await_resume();
	}

private:
	void resume_impl() noexcept override {
		try {
			m_schedule_operation.construct(m_scheduler.schedule());
			m_is_schedule_operation_created = true;

			m_schedule_awaiter.construct(detail::get_awaiter(
				static_cast<schedule_operation&&>(*m_schedule_operation)));
			m_is_schedule_awaiter_created = true;

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
					// Assume it returns a coroutine_handle.
					m_schedule_awaiter->await_suspend(this->m_awaiting_handle).resume();
					return;
				}
			}
		}
		catch (...) {
			// Ignore failure to reschedule and resume inline?
			// Should we catch the exception and rethrow from await_resume()?
			// Or should we require that 'co_await scheduler.schedule()' is noexcept?
		}

		// Resume outside the catch-block.
		this->m_awaiting_handle.resume();
	}

	scheduler& m_scheduler;
	// Can't use std::optional<T> here since T could be a reference.
	detail::manual_lifetime<schedule_operation> m_schedule_operation;
	detail::manual_lifetime<typename detail::awaitable_traits<schedule_operation>::awaiter_t> m_schedule_awaiter;
	bool m_is_schedule_operation_created = false;
	bool m_is_schedule_awaiter_created = false;
};

template<typename sequence, typename traits>
template<typename scheduler>
[[nodiscard]] sequence_barrier_wait_operation<sequence, traits, scheduler> sequence_barrier<sequence, traits>::wait_until_published(
	sequence target_sequence, scheduler& sched) const noexcept {
	return sequence_barrier_wait_operation<sequence, traits, scheduler>(*this, target_sequence, sched);
}

template<typename sequence, typename traits>
void sequence_barrier<sequence, traits>::publish(sequence seq) noexcept {
    // 首先检查是否有等待者，避免不必要的原子 store 操作
    if (m_awaiters.load(std::memory_order_acquire) == nullptr) {
        m_last_published.store(seq, std::memory_order_release);
        return;
    }

    // 更新最后发布的序列号
    m_last_published.store(seq, std::memory_order_release);

    // 获取等待者列表
    awaiter_t* awaiters = m_awaiters.exchange(nullptr, std::memory_order_acq_rel);
    if (awaiters == nullptr) {
        return;
    }

    // 遍历等待者列表，分离需要唤醒的等待者
	awaiter_t* awaiters_to_resume;
	awaiter_t** awaiters_to_resume_tail = &awaiters_to_resume;

	awaiter_t* awaiters_to_requeue;
	awaiter_t** awaiters_to_requeue_tail = &awaiters_to_requeue;

    while (awaiters != nullptr) {
        auto* next = awaiters->m_next;
        if (traits::precedes(seq, awaiters->m_targetSequence)) {
            *awaiters_to_requeue_tail = awaiters;
            awaiters_to_requeue_tail = &awaiters->m_next;
        }
        else {
            *awaiters_to_resume_tail = awaiters;
            awaiters_to_resume_tail = &awaiters->m_next;
        }
        awaiters = next;
    }

	// Null-terminate the two lists.
	*awaiters_to_requeue_tail = nullptr;
	*awaiters_to_resume_tail = nullptr;

    // 重新入队未满足条件的等待者
	if (awaiters_to_requeue != nullptr) {
	    awaiter_t* old_head = m_awaiters.load(std::memory_order_relaxed);
	    do {
	        *awaiters_to_requeue_tail = old_head;
	    } while (!m_awaiters.compare_exchange_weak(
            old_head,
            awaiters_to_requeue,
            std::memory_order_release,
            std::memory_order_relaxed));
	}

    // 唤醒满足条件的等待者
	while (awaiters_to_resume != nullptr) {
		auto* next = awaiters_to_resume->m_next;
		awaiters_to_resume->m_last_known_published = seq;
		awaiters_to_resume->resume();
		awaiters_to_resume = next;
	}
}

template<typename sequence, typename traits>
void sequence_barrier<sequence, traits>::add_awaiter(awaiter_t* awaiter) const noexcept {
	sequence target_sequence = awaiter->m_target_sequence;
	awaiter_t* awaiters_to_requeue = awaiter;
	awaiter_t** awaiters_to_requeue_tail = &awaiter->m_next;

	sequence last_known_published;
	awaiter_t* awaiters_to_resume;
	awaiter_t** awaiters_to_resume_tail = &awaiters_to_resume;

	do {
		// Enqueue the awaiter(s)
		{
			auto* old_head = m_awaiters.load(std::memory_order_relaxed);
			do {
				*awaiters_to_requeue_tail = old_head;
			} while (!m_awaiters.compare_exchange_weak(
				old_head,
				awaiters_to_requeue,
				std::memory_order_seq_cst,
				std::memory_order_relaxed));
		}

		// Check that the sequence we were waiting for wasn't published while
		// we were enqueueing the waiter.
		// This needs to be seq_cst memory order to ensure that in the case that the producer
		// publishes a new sequence number concurrently with this call that we either see
		// their write to m_lastPublished after enqueueing our awaiter, or they see our
		// write to m_awaiters after their write to m_lastPublished.
		last_known_published = m_last_published.load(std::memory_order_seq_cst);
		if (traits::precedes(last_known_published, target_sequence)) {
			// None of the the awaiters we enqueued have been satisfied yet.
			break;
		}

		// Reset the requeue list to empty
		awaiters_to_requeue_tail = &awaiters_to_requeue;

		// At least one of the awaiters we just enqueued is now satisfied by a concurrently
		// published sequence number. The producer thread may not have seen our write to m_awaiters
		// so we need to try to re-acquire the list of awaiters to ensure that the waiters that
		// are now satisfied are woken up.
		auto* awaiters = m_awaiters.exchange(nullptr, std::memory_order_acquire);

		auto min_diff = std::numeric_limits<typename traits::difference_type>::max();

		while (awaiters != nullptr) {
			const auto diff = traits::difference(awaiters->m_target_sequence, last_known_published);
			if (diff > 0) {
				*awaiters_to_requeue_tail = awaiters;
				awaiters_to_requeue_tail = &awaiters->m_next;
				min_diff = diff < min_diff ? diff : min_diff;
			}
			else {
				*awaiters_to_resume_tail = awaiters;
				awaiters_to_resume_tail = &awaiters->m_next;
			}

			awaiters = awaiters->m_next;
		}

		// Null-terminate the list of awaiters to requeue.
		*awaiters_to_requeue_tail = nullptr;

		// Calculate the earliest target sequence required by any of the awaiters to requeue.
		target_sequence = static_cast<sequence>(last_known_published + min_diff);

	} while (awaiters_to_requeue != nullptr);

	// Null-terminate the list of awaiters to resume
	*awaiters_to_resume_tail = nullptr;

	// Resume the awaiters that are ready
	while (awaiters_to_resume != nullptr) {
		auto* next = awaiters_to_resume->m_next;
		awaiters_to_resume->m_last_known_published = last_known_published;
		awaiters_to_resume->resume();
		awaiters_to_resume = next;
	}
}

}

#endif //sequence_BARRIER_H
