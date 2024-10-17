//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/detail/lightweight_manual_reset_event.h"

void coro::detail::lightweight_manual_reset_event::set() noexcept {
    {
        std::lock_guard lock(m_mutex);
        m_is_set = true;
    }
    m_cv.notify_all();
}

void coro::detail::lightweight_manual_reset_event::reset() noexcept {
    std::lock_guard lock(m_mutex);
    m_is_set = false;
}

void coro::detail::lightweight_manual_reset_event::wait() noexcept {
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [this] { return m_is_set; });
}