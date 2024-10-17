//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/cancellation/cancellation_token.h"
#include "../../include/coro/operation_cancelled.h"
#include "../../include/coro/cancellation/cancellation_state.h"

coro::cancellation_token::cancellation_token() noexcept : m_state(nullptr) {}

coro::cancellation_token::cancellation_token(const cancellation_token& other) noexcept : m_state(other.m_state) {
    if (m_state) {
        m_state->add_token_ref();
    }
}

coro::cancellation_token::cancellation_token(cancellation_token&& other) noexcept : m_state(other.m_state) {
    other.m_state = nullptr;
}

coro::cancellation_token::~cancellation_token() {
    if (m_state) {
        m_state->release_token_ref();
    }
}

coro::cancellation_token& coro::cancellation_token::operator=(const cancellation_token& other) noexcept {
    if (other.m_state != m_state) {
        if (m_state) {
            m_state->release_token_ref();
        }

        m_state = other.m_state;

        if (m_state) {
            m_state->add_token_ref();
        }
    }

    return *this;
}

coro::cancellation_token& coro::cancellation_token::operator=(cancellation_token&& other) noexcept {
    if (this != &other) {
        if (m_state) {
            m_state->release_token_ref();
        }

        m_state = other.m_state;
        other.m_state = nullptr;
    }

    return *this;
}

auto coro::cancellation_token::swap(cancellation_token& other) noexcept -> void {
    std::swap(m_state, other.m_state);
}

auto coro::cancellation_token::can_be_cancelled() const noexcept -> bool {
    return m_state && m_state->can_be_cancelled();
}

auto coro::cancellation_token::is_cancellation_requested() const noexcept -> bool {
    return m_state && m_state->is_cancellation_requested();
}

auto coro::cancellation_token::throw_if_cancellation_requested() const -> void {
    if (is_cancellation_requested()) {
        throw operation_cancelled{};
    }
}

coro::cancellation_token::cancellation_token(detail::cancellation_state* state) noexcept : m_state(state) {
    if (m_state) {
        m_state->add_token_ref();
    }
}

