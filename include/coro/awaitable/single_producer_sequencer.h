//
// Created by Jesson on 2024/9/28.
//

#ifndef SINGLE_PRODUCER_SEQUENCER_H
#define SINGLE_PRODUCER_SEQUENCER_H

#include "../sequence_barrier.h"
#include "../sequence_range.h"

namespace coro {

template<typename sequence, typename traits, typename scheduler>
class claim_one_operation;

template<typename sequence, typename traits, typename scheduler>
class claim_operation;

template<typename sequence = std::size_t, typename traits = sequence_traits<sequence>>
class single_producer_sequencer {
public:
	using size_type = typename sequence_range<sequence, traits>::size_type;

	single_producer_sequencer(
		const sequence_barrier<sequence, traits>& barrier, std::size_t size, sequence init_seq = traits::initial_sequence) noexcept
		: m_consumer_barrier(barrier)
		, m_buffer_size(size)
		, m_next_to_claim(init_seq + 1)
		, m_producer_barrier(init_seq) {
	    // Ensure buffer size is a power of two for efficient modulo operation
	    assert((size & (size - 1)) == 0 && "bufferSize must be a power of two");
	}

	/// Claim a slot in the ring buffer asynchronously.
	///
	/// \return
	/// Returns an operation that when awaited will suspend the coroutine until
	/// a slot is available for writing in the ring buffer. The result of the
	/// co_await expression will be the sequence number of the slot.
	/// The caller must publish() the claimed sequence number once they have written to
	/// the ring-buffer.
	template<typename scheduler>
    [[nodiscard]] claim_one_operation<sequence, traits, scheduler> claim_one(scheduler& sched) noexcept {
	    return claim_one_operation<sequence, traits, scheduler>{ *this, sched };
	}

	/// Claim one or more contiguous slots in the ring-buffer.
	///
	/// Use this method over many calls to claim_one() when you have multiple elements to
	/// enqueue. This will claim as many slots as are available up to the specified count
	/// but may claim as few as one slot if only one slot is available.
	///
	/// \param count
	/// The maximum number of slots to claim.
	/// \param sched
	///
	/// \return
	/// Returns an awaitable object that when awaited returns a sequence_range that contains
	/// the range of sequence numbers that were claimed. Once you have written element values
	/// to all of the claimed slots you must publish() the sequence range in order to make
	/// the elements available to consumers.
	template<typename scheduler>
	[[nodiscard]] claim_operation<sequence, traits, scheduler> claim_up_to(std::size_t count, scheduler& sched) noexcept {
	    return claim_operation<sequence, traits, scheduler>(*this, count, sched);
	}

	/// Publish the specified sequence number.
	///
	/// This also implies that all prior sequence numbers have already been published.
	void publish(sequence seq) noexcept {
		m_producer_barrier.publish(seq);
	}

	/// Publish a contiguous range of sequence numbers.
	///
	/// You must have already published all prior sequence numbers.
	///
	/// This is equivalent to just publishing the last sequence number in the range.
	void publish(const sequence_range<sequence, traits>& sequences) noexcept {
		m_producer_barrier.publish(sequences.back());
	}

	/// Query what the last-published sequence number is.
	///
	/// You can assume that all prior sequence numbers are also published.
	sequence last_published() const noexcept {
		return m_producer_barrier.last_published();
	}

	/// Asynchronously wait until the specified sequence number is published.
	///
	/// \param target_seq
	/// The sequence number to wait for.
	/// 
	/// \param sched
	///
	/// \return
	/// Returns an Awaitable type that, when awaited, will suspend the awaiting coroutine until the
	/// specified sequence number has been published.
	///
	/// The result of the 'co_await barrier.wait_until_published(seq)' expression will be the
	/// last-published sequence number, which is guaranteed to be at least 'seq' but may be some
	/// subsequent sequence number if additional items were published while waiting for the
	/// the requested sequence number to be published.
	template<typename scheduler>
	[[nodiscard]]
	auto wait_until_published(sequence target_seq, scheduler& sched) const noexcept {
		return m_producer_barrier.wait_until_published(target_seq, sched);
	}

private:
	template<typename sequence2, typename traits2, typename scheduler>
	friend class claim_operation;

	template<typename sequence2, typename traits2, typename scheduler>
	friend class claim_one_operation;

	const sequence_barrier<sequence, traits>& m_consumer_barrier;
	const std::size_t m_buffer_size;

	alignas(64) sequence m_next_to_claim;

	sequence_barrier<sequence, traits> m_producer_barrier;
};

template<typename sequence, typename traits, typename scheduler>
class claim_one_operation {
public:
	claim_one_operation(single_producer_sequencer<sequence, traits>& sequencer, scheduler& sched) noexcept
		: m_consumer_wait_op(sequencer.m_consumer_barrier, static_cast<sequence>(sequencer.m_next_to_claim - sequencer.m_buffer_size), sched)
		, m_sequencer(sequencer) {}

	bool await_ready() const noexcept {
		return m_consumer_wait_op.await_ready();
	}

	auto await_suspend(std::coroutine_handle<> awaiting_handle) noexcept {
		return m_consumer_wait_op.await_suspend(awaiting_handle);
	}

	sequence await_resume() const noexcept {
		return m_sequencer.m_next_to_claim++;
	}

private:
	sequence_barrier_wait_operation<sequence, traits, scheduler> m_consumer_wait_op;
	single_producer_sequencer<sequence, traits>& m_sequencer;
};

template<typename sequence, typename traits, typename scheduler>
class claim_operation {
public:
	explicit claim_operation(single_producer_sequencer<sequence, traits>& sequencer, std::size_t count, scheduler& sched) noexcept
		: m_consumer_wait_op(sequencer.m_consumer_barrier, static_cast<sequence>(sequencer.m_next_to_claim - sequencer.m_buffer_size), sched)
		, m_sequencer(sequencer)
		, m_count(count) {}

	bool await_ready() const noexcept {
		return m_consumer_wait_op.await_ready();
	}

	auto await_suspend(std::coroutine_handle<> awaiting_handle) noexcept {
		return m_consumer_wait_op.await_suspend(awaiting_handle);
	}

	sequence_range<sequence, traits> await_resume() noexcept {
		const auto last_available_seq = static_cast<sequence>(m_consumer_wait_op.await_resume() + m_sequencer.m_buffer_size);
		const sequence begin = m_sequencer.m_next_to_claim;
		const std::size_t available_count = static_cast<std::size_t>(last_available_seq - begin) + 1;
		const std::size_t count_to_claim = std::min(m_count, available_count);
		const auto end = static_cast<sequence>(begin + count_to_claim);
		m_sequencer.m_next_to_claim = end;
		return sequence_range<sequence, traits>(begin, end);
	}

private:
	sequence_barrier_wait_operation<sequence, traits, scheduler> m_consumer_wait_op;
	single_producer_sequencer<sequence, traits>& m_sequencer;
	std::size_t m_count;
};

}

#endif //SINGLE_PRODUCER_SEQUENCER_H
