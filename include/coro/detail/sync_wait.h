//
// Created by Jesson on 2024/10/17.
//

#ifndef SYNC_WAIT_H
#define SYNC_WAIT_H

#include "lightweight_manual_reset_event.h"
#include "../detail/sync_wait_task.h"
#include "../awaitable_traits.h"

namespace coro {

template<typename AWAITABLE>
auto sync_wait(AWAITABLE&& awaitable) -> typename detail::awaitable_traits<AWAITABLE&&>::await_result_t {
    auto task = detail::make_sync_wait_task(std::forward<AWAITABLE>(awaitable));
    detail::lightweight_manual_reset_event event;
    task.start(event);
    event.wait();
    return task.result();
}

}

#endif //SYNC_WAIT_H
