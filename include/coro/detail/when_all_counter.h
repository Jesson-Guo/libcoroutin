//
// Created by Jesson on 2024/10/18.
//

#ifndef WHEN_ALL_COUNTER_H
#define WHEN_ALL_COUNTER_H

#include <atomic>
#include <coroutine>

namespace coro::detail {

class when_all_counter {
public:
    explicit when_all_counter(std::size_t count) noexcept
        : m_count(count + 1)
        , m_awaiting_handle(nullptr) {}
    
    bool is_ready() const noexcept {
        // We consider this complete if we're asking whether it's ready
        // after a coroutine has already been registered.
        return static_cast<bool>(m_awaiting_handle);
    }

    bool try_await(std::coroutine_handle<> awaiting_handle) noexcept {
        m_awaiting_handle = awaiting_handle;
        return m_count.fetch_sub(1, std::memory_order_acq_rel) > 1;
    }

    void notify_awaitable_completed() noexcept {
        if (m_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            m_awaiting_handle.resume();
        }
    }

protected:
    std::atomic<std::size_t> m_count;
    std::coroutine_handle<> m_awaiting_handle;
};

}


#endif //WHEN_ALL_COUNTER_H
