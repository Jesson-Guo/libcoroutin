//
// Created by Jesson on 2024/10/2.
//

#ifndef SPIN_WAIT_H
#define SPIN_WAIT_H

#include <cstdint>
#include <thread>

namespace coro {

class spin_wait {
public:
    spin_wait() noexcept : spin_count(std::thread::hardware_concurrency() > 1 ? 0 : threshold) {}
    auto next_spin_will_yield() const noexcept -> bool { return spin_count >= threshold; }
    auto spin_one() noexcept -> void;
    auto reset() noexcept -> void { spin_count = std::thread::hardware_concurrency() > 1 ? 0 : threshold; }

private:
    constexpr std::uint32_t threshold = 10;
    std::uint32_t spin_count;
};

}

#endif //SPIN_WAIT_H
