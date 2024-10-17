//
// Created by Jesson on 2024/10/2.
//

#ifndef SPIN_MUTEX_H
#define SPIN_MUTEX_H

#include "spin_wait.h"

#include <atomic>

namespace coro {

class spin_mutex {
public:
    spin_mutex() noexcept : is_locked(false) {}
    auto try_lock() noexcept -> bool { return !is_locked.exchange(true, std::memory_order_acquire); }
    auto lock() noexcept -> void;
    auto unlock() noexcept -> void { is_locked.store(false, std::memory_order_release); }

private:
    std::atomic<bool> is_locked;
};

}

#endif //SPIN_MUTEX_H
