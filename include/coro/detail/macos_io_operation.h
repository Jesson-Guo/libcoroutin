//
// Created by Jesson on 2024/10/11.
//

#ifndef IO_OPERATION_H
#define IO_OPERATION_H

#include "macos.h"
#include "../operation_cancelled.h"
#include "../cancellation/cancellation_token.h"
#include "../cancellation/cancellation_registration.h"

#include <coroutine>
#include <optional>

namespace coro::detail {

class io_operation_base {
public:
    explicit io_operation_base(macos::io_queue& io_queue) noexcept
        : m_io_queue(io_queue)
        , m_message{} {}

    std::size_t get_result() const {
        if (m_message.result < 0) {
            throw std::system_error{ -m_message.result, std::system_category() };
        }

        return m_message.result;
    }

    macos::io_message m_message;
    macos::io_queue& m_io_queue;
};

template<typename OPERATION>
class io_operation : protected io_operation_base {
protected:
    explicit io_operation(macos::io_queue& io_queue) noexcept : io_operation_base(io_queue) {}

public:
    bool await_ready() const noexcept { return false; }

    bool await_suspend(std::coroutine_handle<> awaiting_handle) {
        static_assert(std::is_base_of_v<io_operation, OPERATION>);

        m_message.handle = awaiting_handle;
        return static_cast<OPERATION*>(this)->try_start();
    }

    decltype(auto) await_resume() {
        return static_cast<OPERATION*>(this)->get_result();
    }
};

template<typename OPERATION>
class io_operation_cancellable : protected io_operation<OPERATION> {
protected:
    static constexpr int error_operation_aborted = ECANCELED;

    io_operation_cancellable(macos::io_queue& io_queue, cancellation_token&& ct) noexcept
        : io_operation<OPERATION>(io_queue)
        , m_state(ct.is_cancellation_requested() ? state::completed : state::not_started)
        , m_cancellation_token(std::move(ct)) {}

public:
    bool await_ready() const noexcept {
        return m_state.load(std::memory_order_relaxed) == state::completed;
    }

    bool await_suspend(std::coroutine_handle<> awaiting_handle) noexcept {
        static_assert(std::is_base_of_v<io_operation_cancellable, OPERATION>);

        this->m_message.handle = awaiting_handle;

        if (m_cancellation_token.is_cancellation_requested()) {
            this->m_message.result = error_operation_aborted;
            return false;
        }

        const bool can_be_cancelled = m_cancellation_token.can_be_cancelled();
        m_state.store(state::started, std::memory_order_relaxed);

        const bool will_complete_async = static_cast<OPERATION*>(this)->try_start();
        if (!will_complete_async) {
            this->m_message.result = error_operation_aborted;
            return false;
        }

        if (can_be_cancelled) {
            m_cancellation_registration.emplace(std::move(m_cancellation_token), [this] {
                m_state.store(state::cancellation_requested, std::memory_order_seq_cst);
                static_cast<OPERATION*>(this)->cancel();
            });
        }

        return true;
    }

    decltype(auto) await_resume() {
        if (this->m_message.result == error_operation_aborted) {
            throw operation_cancelled{};
        }
        if (this->m_message.result < 0) {
            if (this->m_message.result == EINTR &&
                m_state.load(std::memory_order_acquire) == state::cancellation_requested) {
                throw operation_cancelled{};
            }
            throw std::system_error{this->m_message.result, std::system_category()};
        }
        return static_cast<OPERATION*>(this)->get_result();
    }

private:
    enum class state {
        not_started,
        started,
        cancellation_requested,
        completed
    };

    std::atomic<state> m_state;
    cancellation_token m_cancellation_token;
    std::optional<cancellation_registration> m_cancellation_registration;
};

}

#endif //IO_OPERATION_H
