//
// Created by Jesson on 2024/9/30.
//

#ifndef ASYNC_LATCH_H
#define ASYNC_LATCH_H

#include "async_manual_reset_event.h"
#include <atomic>

namespace coro {

class async_latch {
public:
    explicit async_latch(std::uintptr_t init_count) noexcept
        : m_count(init_count)
        , m_event(init_count <= 0) {}

    auto is_ready() noexcept -> bool {
        return m_event.is_set();
    }

    auto count_down(std::uintptr_t n=1) noexcept -> void {
        if (m_count.fetch_sub(n, std::memory_order_acq_rel) <= n) {
            m_event.set();
        }
    }

    auto operator co_await() const noexcept {
        return m_event.operator co_await();
    }

private:
    std::atomic<std::uintptr_t> m_count;
    async_manual_reset_event m_event;
};

}

#endif //ASYNC_LATCH_H
