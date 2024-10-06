//
// Created by Jesson on 2024/10/2.
//

#ifndef AUTO_RESET_EVENT_H
#define AUTO_RESET_EVENT_H

#include <condition_variable>
#include <mutex>

namespace coro {

class auto_reset_event {
public:
    explicit auto_reset_event(const bool is_set=false) noexcept : m_is_set(is_set) {}

    ~auto_reset_event() = default;

    auto set() noexcept -> void {
        std::unique_lock lock(m_mutex);
        if (!m_is_set) {
            m_is_set = true;
            m_cv.notify_one();
        }
    }

    auto wait() noexcept -> void {
        std::unique_lock lock(m_mutex);
        while (!m_is_set) {
            m_cv.wait(lock);
        }
        m_is_set = false;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_is_set;
};

}

#endif //AUTO_RESET_EVENT_H
