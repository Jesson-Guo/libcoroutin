//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/cancellation/cancellation_registration.h"
#include "../../include/coro/cancellation/cancellation_state.h"
#include <thread>

coro::cancellation_registration::~cancellation_registration() {
    if (m_state) {
        m_state->deregister_callback(this);
        m_state->release_token_ref();
    }
}

void coro::cancellation_registration::register_callback(cancellation_token&& token) {
    auto* state = token.m_state;
    if (state && state->can_be_cancelled()) {
        m_state = state;
        if (state->try_register_callback(this)) {
            // 设置 token.m_state 为 nullptr：这表示 token 的状态已经转移给了 cancellation_registration，
            // 以确保 token 的状态不会与 cancellation_registration 再次发生交互。
            token.m_state = nullptr;
        }
        else {
            m_state = nullptr;
            m_callback();
        }
    }
    else {
        m_state = nullptr;
    }
}
