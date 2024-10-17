//
// Created by Jesson on 2024/10/17.
//

#ifndef MULTI_PRODUCER_SEQUENCER_H
#define MULTI_PRODUCER_SEQUENCER_H

#include "../sequence_barrier.h"
#include "../sequence_range.h"
#include "../sequence_traits.h"

#include "../detail/manual_lifetime.h"

#include <atomic>
#include <cassert>

namespace coro {

template<typename SEQUENCE, typename TRAITS, typename SCHEDULER>
class claim_one_operation;

template<typename SEQUENCE, typename TRAITS, typename SCHEDULER>
class claim_operation;

template<typename SEQUENCE, typename TRAITS>
class wait_operation_base;

template<typename SEQUENCE, typename TRAITS, typename SCHEDULER>
class wait_operation;

/// A multi-producer sequencer is a thread-synchronisation primitive that can be
/// used to synchronise access to a ring-buffer of power-of-two size where you
/// have multiple producers concurrently claiming slots in the ring-buffer and
/// publishing items.
///
/// When a writer wants to write to a slot in the buffer it first atomically
/// increments a counter by the number of slots it wishes to allocate.
/// It then waits until all of those slots have become available and then
/// returns the range of sequence numbers allocated back to the caller.
/// The caller then writes to those slots and when done publishes them by
/// writing the sequence numbers published to each of the slots to the
/// corresponding element of an array of equal size to the ring buffer.
/// When a reader wants to check if the next sequence number is available
/// it then simply needs to read from the corresponding slot in this array
/// to check if the value stored there is equal to the sequence number it
/// is wanting to read.
///
/// This means concurrent writers are wait-free when there is space available
/// in the ring buffer, requiring a single atomic fetch-add operation as the
/// only contended write operation. All other writes are to memory locations
/// owned by a particular writer. Concurrent writers can publish items out of
/// order so that one writer does not hold up other writers until the ring
/// buffer fills up.
template<typename SEQUENCE = std::size_t, typename TRAITS = sequence_traits<SEQUENCE>>
class multi_producer_sequencer {
public:
	multi_producer_sequencer(const sequence_barrier<SEQUENCE, TRAITS>& barrier, std::size_t size, SEQUENCE init = TRAITS::initial_sequence);

	/// The size of the circular buffer. This will be a power-of-two.
	std::size_t buffer_size() const noexcept { return m_sequence_mask + 1; }

	/// Lookup the last-known-published sequence number after the specified
	/// sequence number.
	SEQUENCE last_published_after(SEQUENCE last_known_published) const noexcept;

	/// Wait until the specified target sequence number has been published.
	///
	/// Returns an awaitable type that when co_awaited will suspend the awaiting
	/// coroutine until the specified 'target_seq' number and all prior sequence
	/// numbers have been published.
	template<typename SCHEDULER>
	wait_operation<SEQUENCE, TRAITS, SCHEDULER> wait_until_published(SEQUENCE target_seq, SEQUENCE last_known_published, SCHEDULER& sched) const noexcept;

	/// Query if there are currently any slots available for claiming.
	///
	/// Note that this return-value is only approximate if you have multiple producers
	/// since immediately after returning true another thread may have claimed the
	/// last available slot.
	bool any_available() const noexcept;

	/// Claim a single slot in the buffer and wait until that slot becomes available.
	///
	/// Returns an Awaitable type that yields the sequence number of the slot that
	/// was claimed.
	///
	/// Once the producer has claimed a slot then they are free to write to that
	/// slot within the ring buffer. Once the value has been initialised the item
	/// must be published by calling the .publish() method, passing the sequence
	/// number.
	template<typename SCHEDULER>
	claim_one_operation<SEQUENCE, TRAITS, SCHEDULER> claim_one(SCHEDULER& sched) noexcept;

	/// Claim a contiguous range of sequence numbers corresponding to slots within
	/// a ring-buffer.
	///
	/// This will claim at most the specified count of sequence numbers but may claim
	/// fewer if there are only fewer entries available in the buffer. But will claim
	/// at least one sequence number.
	///
	/// Returns an awaitable that will yield a sequence_range object containing the
	/// sequence numbers that were claimed.
	///
	/// The caller is responsible for ensuring that they publish every element of the
	/// returned sequence range by calling .publish().
	template<typename SCHEDULER>
	claim_operation<SEQUENCE, TRAITS, SCHEDULER> claim_up_to(std::size_t count, SCHEDULER& sched) noexcept;

	/// Publish the element with the specified sequence number, making it available
	/// to consumers.
	///
	/// Note that different sequence numbers may be published by different producer
	/// threads out of order. A sequence number will not become available to consumers
	/// until all preceding sequence numbers have also been published.
	///
	/// \param seq
	/// The sequence number of the elemnt to publish
	/// This sequence number must have been previously acquired via a call to 'claim_one()'
	/// or 'claim_up_to()'.
	void publish(SEQUENCE seq) noexcept;

	/// Publish a contiguous range of sequence numbers, making each of them available
	/// to consumers.
	///
	/// This is equivalent to calling publish(seq) for each sequence number, seq, in
	/// the specified range, but is more efficient since it only checks to see if
	/// there are coroutines that need to be woken up once.
	void publish(const sequence_range<SEQUENCE, TRAITS>& range) noexcept;

private:
	template<typename SEQUENCE2, typename TRAITS2>
	friend class wait_operation_base;

	template<typename SEQUENCE2, typename TRAITS2, typename SCHEDULER>
	friend class claim_operation;

	template<typename SEQUENCE2, typename TRAITS2, typename SCHEDULER>
	friend class claim_one_operation;

	void resume_ready_awaiters() noexcept;
	void add_awaiter(wait_operation_base<SEQUENCE, TRAITS>* awaiter) const noexcept;

	const sequence_barrier<SEQUENCE, TRAITS>& m_consumer_barrier;
	const std::size_t m_sequence_mask;
	const std::unique_ptr<std::atomic<SEQUENCE>[]> m_published;
	alignas(64) std::atomic<SEQUENCE> m_next_to_claim;
	alignas(64) mutable std::atomic<wait_operation_base<SEQUENCE, TRAITS>*> m_awaiters;
};

template<typename SEQUENCE, typename TRAITS, typename SCHEDULER>
class multi_producer_sequencer_claim_awaiter {
public:
	multi_producer_sequencer_claim_awaiter(
		const sequence_barrier<SEQUENCE, TRAITS>& barrier, std::size_t size, const sequence_range<SEQUENCE, TRAITS>& range, SCHEDULER& sched) noexcept
		: m_barrier_wait(barrier, range.back() - size, sched)
		, m_claimed_range(range) {}

	bool await_ready() const noexcept {
		return m_barrier_wait.await_ready();
	}

	auto await_suspend(std::coroutine_handle<> awaiting_handle) noexcept {
		return m_barrier_wait.await_suspend(awaiting_handle);
	}

	sequence_range<SEQUENCE, TRAITS> await_resume() noexcept {
		return m_claimed_range;
	}

private:
	sequence_barrier_wait_operation<SEQUENCE, TRAITS, SCHEDULER> m_barrier_wait;
	sequence_range<SEQUENCE, TRAITS> m_claimed_range;
};

template<typename SEQUENCE, typename TRAITS, typename SCHEDULER>
class claim_operation {
public:
	claim_operation(multi_producer_sequencer<SEQUENCE, TRAITS>& sequencer, std::size_t count, SCHEDULER& sched) noexcept
		: m_sequencer(sequencer)
		, m_count(count < sequencer.buffer_size() ? count : sequencer.buffer_size())
		, m_scheduler(sched) {}

	multi_producer_sequencer_claim_awaiter<SEQUENCE, TRAITS, SCHEDULER> operator co_await() noexcept {
		// We wait until the awaitable is actually co_await'ed before we claim the
		// range of elements. If we claimed them earlier, then it may be possible for
		// the caller to fail to co_await the result eg. due to an exception, which
		// would leave the sequence numbers unable to be published and would eventually
		// deadlock consumers that waited on them.
		//
		// TODO: We could try and acquire only as many as are available if fewer than
		// m_count elements are available. This would complicate the logic here somewhat
		// as we'd need to use a compare-exchange instead.
		const SEQUENCE first = m_sequencer.m_next_to_claim.fetch_add(m_count, std::memory_order_relaxed);
		return multi_producer_sequencer_claim_awaiter<SEQUENCE, TRAITS, SCHEDULER>{
			m_sequencer.m_consumer_barrier,
			m_sequencer.buffer_size(),
			sequence_range<SEQUENCE, TRAITS>{ first, first + m_count },
			m_scheduler
		};
	}

private:
	multi_producer_sequencer<SEQUENCE, TRAITS>& m_sequencer;
	std::size_t m_count;
	SCHEDULER& m_scheduler;
};

template<typename SEQUENCE, typename TRAITS, typename SCHEDULER>
class claim_one_awaiter {
public:
	claim_one_awaiter(const sequence_barrier<SEQUENCE, TRAITS>& barrier, std::size_t size, SEQUENCE seq, SCHEDULER& sched) noexcept
		: m_wait_op(barrier, seq - size, sched)
		, m_claimed_seq(seq) {}

	bool await_ready() const noexcept {
		return m_wait_op.await_ready();
	}

	auto await_suspend(std::coroutine_handle<> awaiting_handle) noexcept {
		return m_wait_op.await_suspend(awaiting_handle);
	}

	SEQUENCE await_resume() noexcept {
		return m_claimed_seq;
	}

private:
	sequence_barrier_wait_operation<SEQUENCE, TRAITS, SCHEDULER> m_wait_op;
	SEQUENCE m_claimed_seq;
};

template<typename SEQUENCE, typename TRAITS, typename SCHEDULER>
class claim_one_operation {
public:
	claim_one_operation(multi_producer_sequencer<SEQUENCE, TRAITS>& sequencer, SCHEDULER& sched) noexcept
		: m_sequencer(sequencer)
		, m_scheduler(sched) {}

	claim_one_awaiter<SEQUENCE, TRAITS, SCHEDULER> operator co_await() noexcept {
		return claim_one_awaiter<SEQUENCE, TRAITS, SCHEDULER>{
			m_sequencer.m_consumer_barrier,
			m_sequencer.buffer_size(),
			m_sequencer.m_next_to_claim.fetch_add(1, std::memory_order_relaxed),
			m_scheduler
		};
	}

private:
	multi_producer_sequencer<SEQUENCE, TRAITS>& m_sequencer;
	SCHEDULER& m_scheduler;
};

template<typename SEQUENCE, typename TRAITS>
class wait_operation_base {
public:
	wait_operation_base(const multi_producer_sequencer<SEQUENCE, TRAITS>& sequencer, SEQUENCE target_seq, SEQUENCE last_known_published) noexcept
		: m_sequencer(sequencer)
		, m_target_seq(target_seq)
		, m_last_known_published(last_known_published)
		, m_ready_to_resume(false) {}

	wait_operation_base(const wait_operation_base& other) noexcept
		: m_sequencer(other.m_sequencer)
		, m_target_seq(other.m_target_seq)
		, m_last_known_published(other.m_last_known_published)
		, m_ready_to_resume(false) {}

	bool await_ready() const noexcept {
		return !TRAITS::precedes(m_last_known_published, m_target_seq);
	}

	bool await_suspend(std::coroutine_handle<> awaiting_handle) noexcept {
		m_awaiting_handle = awaiting_handle;

		m_sequencer.add_awaiter(this);

		// Mark the waiter as ready to resume.
		// If it was already marked as ready-to-resume within the call to add_awaiter() or
		// on another thread then this exchange() will return true. In this case we want to
		// resume immediately and continue execution by returning false.
		return !m_ready_to_resume.exchange(true, std::memory_order_acquire);
	}

	SEQUENCE await_resume() noexcept {
		return m_last_known_published;
	}

protected:
	friend class multi_producer_sequencer<SEQUENCE, TRAITS>;

	void resume(SEQUENCE last_known_published) noexcept {
		m_last_known_published = last_known_published;
		if (m_ready_to_resume.exchange(true, std::memory_order_release)) {
			resume_impl();
		}
	}

	virtual void resume_impl() noexcept = 0;

	const multi_producer_sequencer<SEQUENCE, TRAITS>& m_sequencer;
	SEQUENCE m_target_seq;
	SEQUENCE m_last_known_published;
	wait_operation_base* m_next;
	std::coroutine_handle<> m_awaiting_handle;
	std::atomic<bool> m_ready_to_resume;
};

template<typename SEQUENCE, typename TRAITS, typename SCHEDULER>
class wait_operation : public wait_operation_base<SEQUENCE, TRAITS> {
	using schedule_operation = decltype(std::declval<SCHEDULER&>().schedule());

public:
	wait_operation(const multi_producer_sequencer<SEQUENCE, TRAITS>& sequencer, SEQUENCE target_seq, SEQUENCE last_known_published, SCHEDULER& sched) noexcept
		: wait_operation_base<SEQUENCE, TRAITS>(sequencer, target_seq, last_known_published)
		, m_scheduler(sched) {}

	wait_operation(const wait_operation& other) noexcept
		: wait_operation_base<SEQUENCE, TRAITS>(other)
		, m_scheduler(other.m_scheduler) {}

	~wait_operation() {
		if (m_is_schedule_awaiter_created) {
			m_schedule_awaiter.destruct();
		}
		if (m_is_schedule_op_created) {
			m_schedule_op.destruct();
		}
	}

	SEQUENCE await_resume() noexcept(noexcept(m_schedule_op->await_resume())) {
		if (m_is_schedule_op_created) {
			m_schedule_op->await_resume();
		}
		return wait_operation_base<SEQUENCE, TRAITS>::await_resume();
	}

private:
	void resume_impl() noexcept override {
		try {
			m_schedule_op.construct(m_scheduler.schedule());
			m_is_schedule_op_created = true;

			m_schedule_awaiter.construct(detail::get_awaiter(static_cast<schedule_operation&&>(*m_schedule_op)));
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

	SCHEDULER& m_scheduler;
	// Can't use std::optional<T> here since T could be a reference.
	detail::manual_lifetime<schedule_operation> m_schedule_op;
	detail::manual_lifetime<typename detail::awaitable_traits<schedule_operation>::awaiter_t> m_schedule_awaiter;
	bool m_is_schedule_op_created = false;
	bool m_is_schedule_awaiter_created = false;
};

template<typename SEQUENCE, typename TRAITS>
multi_producer_sequencer<SEQUENCE, TRAITS>::multi_producer_sequencer(
	const sequence_barrier<SEQUENCE, TRAITS>& barrier, std::size_t size, SEQUENCE init)
	: m_consumer_barrier(barrier)
	, m_sequence_mask(size - 1)
	, m_published(std::make_unique<std::atomic<SEQUENCE>[]>(size))
	, m_next_to_claim(init + 1)
	, m_awaiters(nullptr) {
	// bufferSize must be a positive power-of-two
	assert(size > 0 && (size & (size - 1)) == 0);
	// but must be no larger than the max diff value.
	using diff_t = typename TRAITS::difference_type;
	using unsigned_diff_t = std::make_unsigned_t<diff_t>;
	constexpr auto max_size = static_cast<unsigned_diff_t>(std::numeric_limits<diff_t>::max());
	assert(size <= max_size);

	SEQUENCE seq = init - (size - 1);
	do {
		std::atomic_init(&m_published[seq & m_sequence_mask], seq);
	} while (seq++ != init);
}

template<typename SEQUENCE, typename TRAITS>
SEQUENCE multi_producer_sequencer<SEQUENCE, TRAITS>::last_published_after(SEQUENCE last_known_published) const noexcept {
	const auto mask = m_sequence_mask;
	SEQUENCE seq = last_known_published + 1;
	while (m_published[seq & mask].load(std::memory_order_acquire) == seq) {
		last_known_published = seq++;
	}
	return last_known_published;
}

template<typename SEQUENCE, typename TRAITS>
template<typename SCHEDULER>
wait_operation<SEQUENCE, TRAITS, SCHEDULER>
multi_producer_sequencer<SEQUENCE, TRAITS>::wait_until_published(SEQUENCE target_seq, SEQUENCE last_known_published, SCHEDULER& sched) const noexcept {
	return wait_operation<SEQUENCE, TRAITS, SCHEDULER>{
		*this, target_seq, last_known_published, sched
	};
}

template<typename SEQUENCE, typename TRAITS>
bool multi_producer_sequencer<SEQUENCE, TRAITS>::any_available() const noexcept {
	return TRAITS::precedes(
	    m_next_to_claim.load(std::memory_order_relaxed),
	    m_consumer_barrier.last_published() + buffer_size());
}

template<typename SEQUENCE, typename TRAITS>
template<typename SCHEDULER>
claim_one_operation<SEQUENCE, TRAITS, SCHEDULER> multi_producer_sequencer<SEQUENCE, TRAITS>::claim_one(SCHEDULER& sched) noexcept {
	return claim_one_operation<SEQUENCE, TRAITS, SCHEDULER>{ *this, sched };
}

template<typename SEQUENCE, typename TRAITS>
template<typename SCHEDULER>
claim_operation<SEQUENCE, TRAITS, SCHEDULER>
multi_producer_sequencer<SEQUENCE, TRAITS>::claim_up_to(std::size_t count, SCHEDULER& sched) noexcept {
	return claim_operation<SEQUENCE, TRAITS, SCHEDULER>{ *this, count, sched };
}

template<typename SEQUENCE, typename TRAITS>
void multi_producer_sequencer<SEQUENCE, TRAITS>::publish(SEQUENCE seq) noexcept {
	m_published[seq & m_sequence_mask].store(seq, std::memory_order_seq_cst);

	// Resume any waiters that might have been satisfied by this publish operation.
	resume_ready_awaiters();
}

template<typename SEQUENCE, typename TRAITS>
void multi_producer_sequencer<SEQUENCE, TRAITS>::publish(const sequence_range<SEQUENCE, TRAITS>& range) noexcept {
	if (range.empty()) {
		return;
	}

	// Publish all but the first sequence number using relaxed atomics.
	// No consumer should be reading those subsequent sequence numbers until they've seen
	// that the first sequence number in the range is published.
	for (SEQUENCE seq : range.skip(1)) {
		m_published[seq & m_sequence_mask].store(seq, std::memory_order_relaxed);
	}

	// Now publish the first sequence number with seq_cst semantics.
	m_published[range.front() & m_sequence_mask].store(range.front(), std::memory_order_seq_cst);

	// Resume any waiters that might have been satisfied by this publish operation.
	resume_ready_awaiters();
}

template<typename SEQUENCE, typename TRAITS>
void multi_producer_sequencer<SEQUENCE, TRAITS>::resume_ready_awaiters() noexcept {
	using awaiter_t = wait_operation_base<SEQUENCE, TRAITS>;

	awaiter_t* awaiters = m_awaiters.load(std::memory_order_seq_cst);
	if (awaiters == nullptr) {
		// No awaiters
		return;
	}

	// There were some awaiters. Try to acquire the list of waiters with an
	// atomic exchange as we might be racing with other consumers/producers.
	awaiters = m_awaiters.exchange(nullptr, std::memory_order_seq_cst);
	if (awaiters == nullptr) {
		// Didn't acquire the list
		// Some other thread is now responsible for resuming them. Our job is done.
		return;
	}

	SEQUENCE last_known_published;

	awaiter_t* awaiters_to_resume;
	awaiter_t** awaiters_to_resume_tail = &awaiters_to_resume;

	awaiter_t* awaiters_to_requeue;
	awaiter_t** awaiters_to_requeue_tail = &awaiters_to_requeue;

	do {
		using diff_t = typename TRAITS::difference_type;

		last_known_published = last_published_after(awaiters->m_last_known_published);

		// First scan the list of awaiters and split them into 'requeue' and 'resume' lists.
		auto min_diff = std::numeric_limits<diff_t>::max();
		do {
			auto diff = TRAITS::difference(awaiters->m_target_seq, last_known_published);
			if (diff > 0) {
				// Not ready yet.
				min_diff = diff < min_diff ? diff : min_diff;
				*awaiters_to_requeue_tail = awaiters;
				awaiters_to_requeue_tail = &awaiters->m_next;
			}
			else {
				*awaiters_to_resume_tail = awaiters;
				awaiters_to_resume_tail = &awaiters->m_next;
			}
			awaiters->m_last_known_published = last_known_published;
			awaiters = awaiters->m_next;
		} while (awaiters != nullptr);

		// Null-terinate the requeue list
		*awaiters_to_requeue_tail = nullptr;

		if (awaiters_to_requeue != nullptr) {
			// Requeue the waiters that are not ready yet.
			awaiter_t* old_head = nullptr;
			while (!m_awaiters.compare_exchange_weak(old_head, awaiters_to_requeue, std::memory_order_seq_cst, std::memory_order_relaxed)) {
				*awaiters_to_requeue_tail = old_head;
			}

			// Reset the awaitersToRequeue list
			awaiters_to_requeue_tail = &awaiters_to_requeue;

			const SEQUENCE earliest_target_sequence = last_known_published + min_diff;

			// Now we need to check again to see if any of the waiters we just enqueued
			// is now satisfied by a concurrent call to publish().
			//
			// We need to be a bit more careful here since we are no longer holding any
			// awaiters and so producers/consumers may advance the sequence number arbitrarily
			// far. If the sequence number advances more than buffer_size() ahead of the
			// earliestTargetSequence then the m_published[] array may have sequence numbers
			// that have advanced beyond earliestTargetSequence, potentially even wrapping
			// sequence numbers around to then be preceding where they were before. If this
			// happens then we don't need to worry about resuming any awaiters that were waiting
			// for 'earliestTargetSequence' since some other thread has already resumed them.
			// So the only case we need to worry about here is when all m_published entries for
			// sequence numbers in range [last_known_published + 1, earliestTargetSequence] have
			// published sequence numbers that match the range.
			const auto sequence_mask = m_sequence_mask;
			SEQUENCE seq = last_known_published + 1;
			while (m_published[seq & sequence_mask].load(std::memory_order_seq_cst) == seq) {
				last_known_published = seq;
				if (seq == earliest_target_sequence) {
					// At least one of the awaiters we just published is now satisfied.
					// Reacquire the list of awaiters and continue around the outer loop.
					awaiters = m_awaiters.exchange(nullptr, std::memory_order_acquire);
					break;
				}
				++seq;
			}
		}
	} while (awaiters != nullptr);

	// Null-terminate list of awaiters to resume.
	*awaiters_to_resume_tail = nullptr;

	while (awaiters_to_resume != nullptr) {
		awaiter_t* next = awaiters_to_resume->m_next;
		awaiters_to_resume->resume(last_known_published);
		awaiters_to_resume = next;
	}
}

template<typename SEQUENCE, typename TRAITS>
void multi_producer_sequencer<SEQUENCE, TRAITS>::add_awaiter(wait_operation_base<SEQUENCE, TRAITS>* awaiter) const noexcept {
	using awaiter_t = wait_operation_base<SEQUENCE, TRAITS>;

	SEQUENCE target_seq = awaiter->m_target_seq;
	SEQUENCE last_known_published = awaiter->m_last_known_published;

	awaiter_t* awaiters_to_enqueue = awaiter;
	awaiter_t** awaiters_to_enqueue_tail = &awaiter->m_next;

	awaiter_t* awaiters_to_resume;
	awaiter_t** awaiters_to_resume_tail = &awaiters_to_resume;

	const SEQUENCE sequence_mask = m_sequence_mask;

	do {
		// Enqueue the awaiters.
		{
			awaiter_t* old_head = m_awaiters.load(std::memory_order_relaxed);
			do {
				*awaiters_to_enqueue_tail = old_head;
			} while (!m_awaiters.compare_exchange_weak(
				old_head,
				awaiters_to_enqueue,
				std::memory_order_seq_cst,
				std::memory_order_relaxed));
		}

		// Reset list of waiters
		awaiters_to_enqueue_tail = &awaiters_to_enqueue;

		// Check to see if the last-known published sequence number has advanced
		// while we were enqueuing the awaiters. Need to use seq_cst memory order
		// here to ensure that if there are concurrent calls to publish() that would
		// wake up any of the awaiters we just enqueued that either we will see their
		// write to m_published slots or they will see our write to m_awaiters.
		//
		// Note also, that we are assuming that the last-known published sequence is
		// not going to advance more than buffer_size() ahead of target_seq since
		// there is at least one consumer that won't be resumed and so thus can't
		// publish the sequence number it's waiting for to its sequence_barrier and so
		// producers won't be able to claim its slot in the buffer.
		//
		// TODO: Check whether we can weaken the memory order here to just use 'seq_cst' on the
		// first .load() and then use 'acquire' on subsequent .load().
		while (m_published[(last_known_published + 1) & sequence_mask].load(std::memory_order_seq_cst) == (last_known_published + 1)) {
			++last_known_published;
		}

		if (!TRAITS::precedes(last_known_published, target_seq)) {
			// At least one awaiter we just enqueued has now been satisified.
			// To ensure it is woken up we need to reacquire the list of awaiters and resume
			awaiter_t* awaiters = m_awaiters.exchange(nullptr, std::memory_order_acquire);

			using diff_t = typename TRAITS::difference_type;

			diff_t minDiff = std::numeric_limits<diff_t>::max();

			while (awaiters != nullptr) {
				diff_t diff = TRAITS::difference(target_seq, last_known_published);
				if (diff > 0) {
					// Not yet ready.
					minDiff = diff < minDiff ? diff : minDiff;
					*awaiters_to_enqueue_tail = awaiters;
					awaiters_to_enqueue_tail = &awaiters->m_next;
					awaiters->m_last_known_published = last_known_published;
				}
				else {
					// Now ready.
					*awaiters_to_resume_tail = awaiters;
					awaiters_to_resume_tail = &awaiters->m_next;
				}
				awaiters = awaiters->m_next;
			}

			// Calculate the earliest sequence number that any awaiters in the
			// awaitersToEnqueue list are waiting for. We'll use this next time
			// around the loop.
			target_seq = static_cast<SEQUENCE>(last_known_published + minDiff);
		}

		// Null-terminate list of awaiters to enqueue.
		*awaiters_to_enqueue_tail = nullptr;

	} while (awaiters_to_enqueue != nullptr);

	// Null-terminate awaiters to resume.
	*awaiters_to_resume_tail = nullptr;

	// Finally, resume any awaiters we've found that are ready to go.
	while (awaiters_to_resume != nullptr) {
		// Read m_next before calling .resume() as resuming could destroy the awaiter.
		awaiter_t* next = awaiters_to_resume->m_next;
		awaiters_to_resume->resume(last_known_published);
		awaiters_to_resume = next;
	}
}

}

#endif //MULTI_PRODUCER_SEQUENCER_H
