//
// Created by Jesson on 2024/11/5.
//

#ifndef INLINE_SCHEDULER_H
#define INLINE_SCHEDULER_H

namespace coro {

class inline_scheduler {
public:
    inline_scheduler() noexcept = default;
    std::suspend_never schedule() const noexcept { return {}; }
};

}

#endif //INLINE_SCHEDULER_H
