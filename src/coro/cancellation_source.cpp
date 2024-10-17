//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/cancellation/cancellation_source.h"
#include "../../include/coro/cancellation/cancellation_state.h"

coro::cancellation_source::cancellation_source() : m_state(detail::cancellation_state::create()) {}

coro::cancellation_source::cancellation_source(const cancellation_source& other) noexcept : m_state(other.m_state) {
    if (m_state) {
        m_state->add_source_ref();
    }
}

coro::cancellation_source::cancellation_source(cancellation_source&& other) noexcept : m_state(other.m_state) {
    other.m_state = nullptr;
}

coro::cancellation_source::~cancellation_source() {
    if (m_state) {
        m_state->release_source_ref();
    }
}

coro::cancellation_source& coro::cancellation_source::operator=(const cancellation_source& other) noexcept {
    if (m_state != other.m_state) {
        if (m_state) {
            m_state->release_source_ref();
        }
        m_state = other.m_state;
        if (m_state) {
            m_state->add_source_ref();
        }
    }

    return *this;
}

coro::cancellation_source& coro::cancellation_source::operator=(cancellation_source&& other) noexcept {
    if (this != &other) {
        if (m_state) {
            m_state->release_source_ref();
        }
        m_state = other.m_state;
        other.m_state = nullptr;
    }

    return *this;
}

bool coro::cancellation_source::can_be_cancelled() const noexcept {
    return m_state != nullptr;
}

coro::cancellation_token coro::cancellation_source::token() const noexcept {
    return cancellation_token(m_state);
}

void coro::cancellation_source::request_cancellation() const {
    if (m_state) {
        m_state->request_cancellation();
    }
}

bool coro::cancellation_source::is_cancellation_requested() const noexcept {
    return m_state && m_state->is_cancellation_requested();
}

