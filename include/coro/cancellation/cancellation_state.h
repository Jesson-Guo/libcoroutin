//
// Created by Jesson on 2024/10/3.
//

#ifndef CANCELLATION_STATE_H
#define CANCELLATION_STATE_H

#include "cancellation_token.h"

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
        m_state.fetch_add(token_ref_increment, std::memory_order_relaxed);
    }

    /// Decrement the reference count of cancellation_token and
    /// cancellation_registration objects referencing this state.
    void release_token_ref() noexcept {
        const std::uint64_t old_state = m_state.fetch_sub(token_ref_increment, std::memory_order_acq_rel);
        if ((old_state & ref_count_mask) == token_ref_increment) {
            delete this;
        }
    }

    /// Increment the reference count of cancellation_source objects.
    void add_source_ref() noexcept {
        m_state.fetch_add(source_ref_increment, std::memory_order_relaxed);
    }

    /// Decrement the reference count of cancellation_source objects.
    ///
    /// The cancellation_state will no longer be cancellable once the
    /// cancellation_source ref count reaches zero.
    void release_source_ref() noexcept {
        const std::uint64_t old_state = m_state.fetch_sub(source_ref_increment, std::memory_order_acq_rel);
        if ((old_state & ref_count_mask) == source_ref_increment) {
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
        return (m_state.load(std::memory_order_acquire) & requested_flag) != 0;
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
    void deregister_callback(cancellation_registration* registration) const noexcept;

private:
    cancellation_state() noexcept
        : m_state(source_ref_increment)
        , m_registration_state(nullptr) {}

    bool is_cancellation_notification_complete() const noexcept {
        return (m_state.load(std::memory_order_acquire) & notification_complete_flag) != 0;
    }

    static constexpr std::uint64_t requested_flag = 1;
    static constexpr std::uint64_t notification_complete_flag = 2;
    static constexpr std::uint64_t source_ref_increment = 4;
    static constexpr std::uint64_t token_ref_increment = UINT64_C(1) << 33;
    static constexpr std::uint64_t can_be_cancelled_mask = token_ref_increment - 1;
    static constexpr std::uint64_t ref_count_mask = ~(requested_flag | notification_complete_flag);

    // A value that has:
    // - bit 0 - indicates whether cancellation has been requested.
    // - bit 1 - indicates whether cancellation notification is complete.
    // - bits 2-32 - ref-count for cancellation_source instances.
    // - bits 33-63 - ref-count for cancellation_token/cancellation_registration instances.
    std::atomic<std::uint64_t> m_state;
    std::atomic<cancellation_registration_state*> m_registration_state;
};

}

#endif //CANCELLATION_STATE_H
