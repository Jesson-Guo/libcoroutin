//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/auto_reset_event.h"

auto coro::auto_reset_event::set() noexcept -> void {
    std::unique_lock lock(m_mutex);
    if (!m_is_set) {
        m_is_set = true;
        m_cv.notify_one();
    }
}

auto coro::auto_reset_event::wait() noexcept -> void {
    std::unique_lock lock(m_mutex);
    while (!m_is_set) {
        m_cv.wait(lock);
    }
    m_is_set = false;
}
