//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/spin_mutex.h"

auto coro::spin_mutex::lock() noexcept -> void {
    spin_wait wait;
    // attempt to acquire the lock, if it is locked, waiting
    while (!try_lock()) {
        while (is_locked.load(std::memory_order_acquire)) {
            wait.spin_one();
        }
    }
}
