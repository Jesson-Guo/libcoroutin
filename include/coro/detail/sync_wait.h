//
// Created by Jesson on 2024/10/17.
//

#ifndef SYNC_WAIT_H
#define SYNC_WAIT_H

#include "lightweight_manual_reset_event.h"
#include "../detail/sync_wait_task.h"
#include "../awaitable_traits.h"

namespace coro {

template<typename awaitable_type>
auto sync_wait(awaitable_type&& awaitable) -> typename detail::awaitable_traits<awaitable_type&&>::await_result_t {
    auto task = detail::make_sync_wait_task(std::forward<awaitable_type>(awaitable));
    detail::lightweight_manual_reset_event event;
    task.start(event);
    event.wait();
    return task.result();
}

}

#endif //SYNC_WAIT_H
