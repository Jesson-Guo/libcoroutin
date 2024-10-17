//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/spin_wait.h"

auto coro::spin_wait::spin_one() noexcept -> void {
    if (next_spin_will_yield()) {
        std::this_thread::yield();
    }
    ++spin_count;
    if (spin_count == 0) {
        spin_count = threshold;
    }
}
