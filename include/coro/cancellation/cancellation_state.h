//
// Created by Jesson on 2024/10/3.
//

#ifndef CANCELLATION_STATE_H
#define CANCELLATION_STATE_H

#include "cancellation_token.h"
#include "cancellation_registration.h"

#include <thread>
#include <atomic>
#include <cstdint>

namespace coro::detail {

struct cancellation_registration_state;

class cancellation_state {
public:

    /// Allocates a new cancellation_state object.
    ///
    /// \throw std::bad_alloc
    /// If there was insufficient memory to allocate one.
    static cancellation_state* create() {
        return new cancellation_state();
    }

    ~cancellation_state();

    /// Increment the reference count of cancellation_token and
    /// cancellation_registration objects referencing this state.
    void add_token_ref() noexcept {
        m_state.fetch_add(cancellation_token_ref_increment, std::memory_order_relaxed);
    }

    /// Decrement the reference count of cancellation_token and
    /// cancellation_registration objects referencing this state.
    void release_token_ref() noexcept {
        const std::uint64_t old_state = m_state.fetch_sub(cancellation_token_ref_increment, std::memory_order_acq_rel);
        if ((old_state & cancellation_ref_count_mask) == cancellation_token_ref_increment) {
            delete this;
        }
    }

    /// Increment the reference count of cancellation_source objects.
    void add_source_ref() noexcept {
        m_state.fetch_add(cancellation_source_ref_increment, std::memory_order_relaxed);
    }

    /// Decrement the reference count of cancellation_source objects.
    ///
    /// The cancellation_state will no longer be cancellable once the
    /// cancellation_source ref count reaches zero.
    void release_source_ref() noexcept {
        const std::uint64_t old_state = m_state.fetch_sub(cancellation_source_ref_increment, std::memory_order_acq_rel);
        if ((old_state & cancellation_ref_count_mask) == cancellation_source_ref_increment) {
            delete this;
        }
    }

    /// Query if the cancellation_state can have cancellation requested.
    ///
    /// \return
    /// Returns true if there are no more references to a cancellation_source
    /// object.
    bool can_be_cancelled() const noexcept {
        return (m_state.load(std::memory_order_acquire) & can_be_cancelled_mask) != 0;
    }

    /// Query if some thread has called request_cancellation().
    bool is_cancellation_requested() const noexcept {
        return (m_state.load(std::memory_order_acquire) & cancellation_requested_flag) != 0;
    }

    /// Flag state has having cancellation_requested and execute any
    /// registered callbacks.
    void request_cancellation();

    /// Try to register the cancellation_registration as a callback to be executed
    /// when cancellation is requested.
    ///
    /// \return
    /// true if the callback was successfully registered, false if the callback was
    /// not registered because cancellation had already been requested.
    ///
    /// \throw std::bad_alloc
    /// If callback was unable to be registered due to insufficient memory.
    bool try_register_callback(cancellation_registration* registration);

    /// Deregister a callback previously registered successfully in a call to try_register_callback().
    ///
    /// If the callback is currently being executed on another
    /// thread that is concurrently calling request_cancellation()
    /// then this call will block until the callback has finished executing.
    void deregister_callback(cancellation_registration* registration) noexcept;

private:

    cancellation_state() noexcept
        : m_state(cancellation_source_ref_increment)
        , m_registration_state(nullptr) {}

    bool is_cancellation_notification_complete() const noexcept {
        return (m_state.load(std::memory_order_acquire) & cancellation_notification_complete_flag) != 0;
    }

    static constexpr std::uint64_t cancellation_requested_flag = 1;
    static constexpr std::uint64_t cancellation_notification_complete_flag = 2;
    static constexpr std::uint64_t cancellation_source_ref_increment = 4;
    static constexpr std::uint64_t cancellation_token_ref_increment = UINT64_C(1) << 33;
    static constexpr std::uint64_t can_be_cancelled_mask = cancellation_token_ref_increment - 1;
    static constexpr std::uint64_t cancellation_ref_count_mask =
        ~(cancellation_requested_flag | cancellation_notification_complete_flag);

    // A value that has:
    // - bit 0 - indicates whether cancellation has been requested.
    // - bit 1 - indicates whether cancellation notification is complete.
    // - bits 2-32 - ref-count for cancellation_source instances.
    // - bits 33-63 - ref-count for cancellation_token/cancellation_registration instances.
    std::atomic<std::uint64_t> m_state;

    std::atomic<cancellation_registration_state*> m_registration_state;

};

}

auto coro::detail::cancellation_registration_list_chunk::allocate(std::uint32_t entry_count)
    -> cancellation_registration_list_chunk* {
    auto chunk_size = sizeof(cancellation_registration_list_chunk) + (entry_count - 1) * sizeof(m_entries[0]);
    auto* chunk = static_cast<cancellation_registration_list_chunk*>(std::malloc(chunk_size);

    if (!chunk) {
        throw std::bad_alloc{};
    }

    ::new (&chunk->m_next_chunk) std::atomic<cancellation_registration_list_chunk*>(nullptr);
    chunk->m_prev_chunk = nullptr;
    ::new (&chunk->m_approx_free_count) std::atomic(static_cast<std::int32_t>(entry_count - 1));
    chunk->m_entry_count = entry_count;
    for (std::uint32_t i = 0; i < entry_count; ++i) {
        ::new (&chunk->m_entries[i]) std::atomic<cancellation_registration*>(nullptr);
    }
    return chunk;
}

auto coro::detail::cancellation_registration_list::allocate() -> cancellation_registration_list* {
    constexpr std::uint32_t initial_chunk_size = 16;
    constexpr std::size_t buffer_size =
        sizeof(cancellation_registration_list) +
        (initial_chunk_size - 1) * sizeof(cancellation_registration_list_chunk::m_entries[0]);

    auto* bucket = static_cast<cancellation_registration_list*>(std::malloc(buffer_size));
    if (!bucket) {
        throw std::bad_alloc{};
    }

    ::new (&bucket->m_approx_tail) std::atomic(&bucket->m_head_chunk);
    ::new (&bucket->m_head_chunk.m_next_chunk) std::atomic<cancellation_registration_list_chunk*>(nullptr);
    bucket->m_head_chunk.m_prev_chunk = nullptr;
    ::new (&bucket->m_head_chunk.m_approx_free_count) std::atomic(static_cast<std::int32_t>(initial_chunk_size - 1));
    bucket->m_head_chunk.m_entry_count = initial_chunk_size;
    for (std::uint32_t i = 0; i < initial_chunk_size; ++i) {
        ::new (&bucket->m_head_chunk.m_entries[i]) std::atomic<cancellation_registration*>(nullptr);
    }
    return bucket;
}

auto coro::detail::cancellation_registration_state::allocate() -> cancellation_registration_state* {
    constexpr std::uint32_t max_list_count = 16;

    auto list_count = std::thread::hardware_concurrency();
    if (list_count > max_list_count) {
        list_count = max_list_count;
    }
    else if (list_count == 0) {
        list_count = 1;
    }

    const std::size_t buffer_size = sizeof(cancellation_registration_state) + (list_count - 1) * sizeof(m_lists[0]);

    auto* state = static_cast<cancellation_registration_state*>(std::malloc(buffer_size));
    if (!state) {
        throw std::bad_alloc{};
    }

    state->m_list_count = list_count;
    for (std::uint32_t i = 0; i < list_count; ++i) {
        ::new (&state->m_lists[i]) std::atomic<cancellation_registration_list*>(nullptr);
    }
    return state;
}

auto coro::detail::cancellation_registration_state::add_registration(
    cancellation_registration* registration) -> cancellation_registration_result {
    // 根据当前线程选择要添加的列表，以减少多个线程同时注册回调时发生争用的机会。
    const auto thread_id_hash_code = std::hash<std::thread::id>{}(std::this_thread::get_id());
	auto& list_ptr = m_lists[thread_id_hash_code % m_list_count];

	auto* list = list_ptr.load(std::memory_order_acquire);
    // 如果 list 尚未初始化（即 nullptr），则分配并初始化一个新的 list。
	if (!list) {
		auto* new_list = cancellation_registration_list::allocate();

		// Pre-claim the first slot.
		registration->m_chunk = &new_list->m_head_chunk;
		registration->m_entry_id = 0;
		::new (&new_list->m_head_chunk.m_entries[0]) std::atomic(registration);

        if (list_ptr.compare_exchange_strong(
                list, new_list, std::memory_order_seq_cst, std::memory_order_acquire)) {
            return {&new_list->m_head_chunk, 0};
		}
	    cancellation_registration_list::free(new_list);
	}

	while (true) {
		// Navigate to the end of the chain of chunks and work backwards looking for a free slot.
		auto* const original_last_chunk = list->m_approx_tail.load(std::memory_order_acquire);

		auto* last_chunk = original_last_chunk;
		for (auto* next = last_chunk->m_next_chunk.load(std::memory_order_acquire);
			next != nullptr;
			next = next->m_next_chunk.load(std::memory_order_acquire)) {
			last_chunk = next;
		}

		if (last_chunk != original_last_chunk) {
			// Update the cache of last chunk pointer so that subsequent
			// registration requests can start there instead.
			// Doesn't matter if these writes race as it will eventually
			// converge to the true last chunk.
			list->m_approx_tail.store(last_chunk, std::memory_order_release);
		}

		for (auto* chunk = last_chunk; chunk != nullptr; chunk = chunk->m_prev_chunk) {
			auto free_count = chunk->m_approx_free_count.load(std::memory_order_relaxed);

			// If it looks like there are no free slots then decrement the count again
			// to force it to re-search every so-often, just in case the count has gotten
			// out-of-sync with the true free count and is reporting none free even though
			// there are some (or possibly all) free slots.
			if (free_count < 1) {
				--free_count;
				chunk->m_approx_free_count.store(free_count, std::memory_order_relaxed);
			}

			constexpr std::int32_t forced_search_threshold = -10;
			if (free_count > 0 || free_count < forced_search_threshold) {
				const auto entry_count = chunk->m_entry_count;
				const auto id_mask = entry_count - 1;
				const auto start_id = entry_count - free_count;

				registration->m_chunk = chunk;

				for (auto i = 0; i < entry_count; ++i) {
					const auto entry_id = (start_id + i) & id_mask;
					auto& entry = chunk->m_entries[entry_id];

					// Do a cheap initial read of the entry value to see if the
					// entry is likely free. This can potentially read stale values
					// and so may lead to falsely thinking it's free or falsely
					// thinking it's occupied. But approximate is good enough here.
					auto* entry_value = entry.load(std::memory_order_relaxed);
					if (!entry_value) {
						registration->m_entry_id = entry_id;
						if (entry.compare_exchange_strong(
							entry_value,
							registration,
							std::memory_order_seq_cst,
							std::memory_order_relaxed)) {
							// Successfully claimed the slot.
							const std::int32_t new_free_count = free_count < 0 ? 0 : free_count - 1;
							chunk->m_approx_free_count.store(new_free_count, std::memory_order_relaxed);
							return {chunk, entry_id};
						}
					}
				}

				// Read through all elements of chunk with no success.
				// Clear free-count back to 0.
				chunk->m_approx_free_count.store(0, std::memory_order_relaxed);
			}
		}

		// We've traversed through all of the chunks and found no free slots.
		// So try and allocate a new chunk and append it to the list.

		constexpr std::uint32_t max_element_count = 1024;
		const std::uint32_t element_count =
			last_chunk->m_entry_count < max_element_count ?
			last_chunk->m_entry_count * 2 : max_element_count;

		// May throw std::bad_alloc if out of memory.
		auto* new_chunk = cancellation_registration_list_chunk::allocate(element_count);
		new_chunk->m_prev_chunk = last_chunk;

		// Pre-allocate first slot.
		registration->m_chunk = new_chunk;
		registration->m_entry_id = 0;
        ::new (&new_chunk->m_entries[0]) std::atomic(registration);

        if (cancellation_registration_list_chunk* old_next = nullptr;
            last_chunk->m_next_chunk.compare_exchange_strong(
			old_next,
			new_chunk,
			std::memory_order_seq_cst,
			std::memory_order_relaxed)) {
			list->m_approx_tail.store(new_chunk, std::memory_order_release);
			return {new_chunk, 0};
		}

		// Some other thread published a new chunk to the end of the list
		// concurrently. Free our chunk and go around the loop again, hopefully
		// allocating a slot from the chunk the other thread just allocated.
        cancellation_registration_list_chunk::free(new_chunk);
	}
}

coro::detail::cancellation_state::~cancellation_state() {
    assert((m_state.load(std::memory_order_relaxed) & cancellation_ref_count_mask) == 0);

    // Use relaxed memory order in reads here since we should already have visibility
    // to all writes as the ref-count decrement that preceded the call to the destructor
    // has acquire-release semantics.

    if (auto* registration_state = m_registration_state.load(std::memory_order_relaxed)) {
        for (std::uint32_t i = 0; i < registration_state->m_list_count; ++i) {
            if (auto* list = registration_state->m_lists[i].load(std::memory_order_relaxed)) {
                auto* chunk = list->m_head_chunk.m_next_chunk.load(std::memory_order_relaxed);
                cancellation_registration_list::free(list);

                while (chunk) {
                    auto* next = chunk->m_next_chunk.load(std::memory_order_relaxed);
                    cancellation_registration_list_chunk::free(chunk);
                    chunk = next;
                }
            }
        }

        cancellation_registration_state::free(registration_state);
    }
}

void coro::detail::cancellation_state::request_cancellation() {
    const auto old_state = m_state.fetch_or(cancellation_requested_flag, std::memory_order_seq_cst);
	if ((old_state & cancellation_requested_flag) != 0) {
		// Some thread has already called request_cancellation().
		return;
	}

	// We are the first caller of request_cancellation.
	// Need to execute any registered callbacks to notify them of cancellation.

	// NOTE: We need to use sequentially-consistent operations here to ensure
	// that if there is a concurrent call to try_register_callback() on another
	// thread that either the other thread will read the prior write to m_state
    // after they write to a registration slot or we will read their write to the
    // registration slot after the prior write to m_state.

	if (auto* const registration_state = m_registration_state.load(std::memory_order_seq_cst)) {
		// Note that there should be no data-race in writing to this value here
		// as another thread will only read it if they are trying to deregister
		// a callback and that fails because we have acquired the pointer to
		// the registration inside the loop below. In this case the atomic
		// exchange that acquires the pointer below acts as a release-operation
		// that synchronises with the failed exchange operation in deregister_callback()
		// which has acquire semantics and thus will have visibility of the write to
		// the m_notificationThreadId value.
		registration_state->m_notification_thread_id = std::this_thread::get_id();

		for (std::uint32_t list_index = 0, list_count = registration_state->m_list_count;
			list_index < list_count;
			++list_index) {
			auto* list = registration_state->m_lists[list_index].load(std::memory_order_seq_cst);
			if (!list) {
				continue;
			}

			auto* chunk = &list->m_head_chunk;
			do {
				for (std::uint32_t entry_index = 0, entryCount = chunk->m_entry_count;
					entry_index < entryCount;
					++entry_index) {
                    auto& entry = chunk->m_entries[entry_index];

                    // Quick read-only operation to check if any registration
					// is present.
					if (auto* registration = entry.load(std::memory_order_seq_cst)) {
						// Try to acquire ownership of the registration by replacing its
						// slot with nullptr atomically. This resolves the race between
						// a concurrent call to deregister_callback() from the registration's
						// destructor.
						registration = entry.exchange(nullptr, std::memory_order_seq_cst);
						if (registration) {
							try {
								registration->m_callback();
							}
							catch (...) {
								// TODO: What should behaviour of unhandled exception in a callback be here?
								std::terminate();
							}
						}
					}
				}

				chunk = chunk->m_next_chunk.load(std::memory_order_seq_cst);
            } while (chunk);
        }

        m_state.fetch_add(cancellation_notification_complete_flag, std::memory_order_release);
	}
}

bool coro::detail::cancellation_state::try_register_callback(cancellation_registration* registration) {
    if (is_cancellation_requested()) {
		return false;
	}

	auto* registration_state = m_registration_state.load(std::memory_order_acquire);
	if (!registration_state) {
		// Could throw std::bad_alloc
		auto* new_registration_state = cancellation_registration_state::allocate();

		// Need to use 'sequentially consistent' on the write here to ensure that if
		// we subsequently read a value from m_state at the end of this function that
		// doesn't have the cancellation_requested_flag bit set that a subsequent call
		// in another thread to request_cancellation() will see this write.
		if (m_registration_state.compare_exchange_strong(
			registration_state,
			new_registration_state,
			std::memory_order_seq_cst,
			std::memory_order_acquire)) {
			registration_state = new_registration_state;
		}
		else {
			cancellation_registration_state::free(new_registration_state);
		}
	}

	// Could throw std::bad_alloc
	auto result = registration_state->add_registration(registration);

	// Need to check status again to handle the case where
	// another thread calls request_cancellation() concurrently
	// but doesn't see our write to the registration list.
	//
	// Note, we don't call IsCancellationRequested() here since that
	// only provides 'acquire' memory semantics and we need 'seq_cst'
	// semantics.
	if ((m_state.load(std::memory_order_seq_cst) & cancellation_requested_flag) != 0) {
		// Cancellation was requested concurrently with adding the
		// registration to the list. Try to remove the registration.
		// If successful we return false to indicate that the callback
		// has not been registered and the caller should execute the
		// callback. If it fails it means that the thread that requested
		// cancellation will execute our callback and we need to wait
		// until it finishes before returning.
		auto& entry = result.m_chunk->m_entries[result.m_entry_id];

		// Need to use compare_exchange here rather than just exchange since
		// it may be possible that the thread calling request_cancellation()
		// acquired our registration and executed the callback, freeing up
		// the slot and then a third thread registers a new registration
		// that gets allocated to this slot.
		//
		// Can use relaxed memory order here since in the case that this succeeds
		// no other thread will have written to the cancellation_registration record
		// so we can safely read from the record without synchronisation.
		auto* old_value = registration;
		const bool deregistered_successfully =
			entry.compare_exchange_strong(old_value, nullptr, std::memory_order_relaxed);
		if (deregistered_successfully) {
			return false;
		}

		// Otherwise, the cancelling thread has taken ownership for executing
        // the callback and we can just act as if the registration succeeded.
    }

    return true;
}

void coro::detail::cancellation_state::deregister_callback(cancellation_registration* registration) noexcept {
    auto* chunk = registration->m_chunk;
    auto& entry = chunk->m_entries[registration->m_entry_id];

    // Use 'acquire' memory order on failure case so that we synchronise with the write
    // to the slot inside request_cancellation() that acquired the registration such that
    // we have visibility of its prior write to m_notifyingThreadId.
    //
    // Could use 'relaxed' memory order on success case as if this succeeds it means that
    // no thread will have written to the registration object.
    auto* old_value = registration;
    const bool deregistered_successfully = entry.compare_exchange_strong(
        old_value,
        nullptr,
        std::memory_order_acquire);
    if (deregistered_successfully) {
        // Increment free-count if it won't make it larger than entry count.
        const std::int32_t old_free_count = chunk->m_approx_free_count.load(std::memory_order_relaxed);
        if (old_free_count < static_cast<std::int32_t>(chunk->m_entry_count)) {
            const std::int32_t new_free_count = old_free_count < 0 ? 1 : old_free_count + 1;
            chunk->m_approx_free_count.store(new_free_count, std::memory_order_relaxed);
        }
    }
    else {
        // A thread executing request_cancellation() has acquired this callback and
        // is executing it. Need to wait until it finishes executing before we return
        // and the registration object is destructed.
        //
        // However, we also need to handle the case where the registration is being
        // removed from within a callback which would otherwise deadlock waiting
        // for the callbacks to finish executing.

        // Use relaxed memory order here as we should already have visibility
        // of the write to m_registrationState from when the registration was first
        // registered.
        auto* registration_state = m_registration_state.load(std::memory_order_relaxed);
        if (std::this_thread::get_id() != registration_state->m_notification_thread_id) {
            // TODO: More efficient busy-wait backoff strategy
            while (!is_cancellation_notification_complete()) {
                std::this_thread::yield();
            }
        }
    }
}

#endif //CANCELLATION_STATE_H
