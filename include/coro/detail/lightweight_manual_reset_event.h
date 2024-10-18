//
// Created by Jesson on 2024/10/17.
//

#ifndef LIGHTWEIGHT_MANUAL_RESET_EVENT_H
#define LIGHTWEIGHT_MANUAL_RESET_EVENT_H

#include <mutex>
#include <condition_variable>

namespace coro::detail {

class lightweight_manual_reset_event {
public:
    explicit lightweight_manual_reset_event(bool init = false) : m_is_set(init) {}
    ~lightweight_manual_reset_event() = default;
    void set() noexcept;
    void reset() noexcept;
    void wait() noexcept;

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_is_set;
};

}

#endif //LIGHTWEIGHT_MANUAL_RESET_EVENT_H
