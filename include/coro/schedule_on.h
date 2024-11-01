//
// Created by Jesson on 2024/10/24.
//

#ifndef SCHEDULE_ON_H
#define SCHEDULE_ON_H

#include <coro/types/task.h>
#include <coro/types/shared_task.h>
#include <coro/types/async_generator.h>
#include <coro/awaitable_traits.h>

#include <coro/detail/remove_rvalue_reference.h>

namespace coro {

template<typename SCHEDULER>
struct schedule_on_transform {
    explicit schedule_on_transform(SCHEDULER& scheduler) noexcept : scheduler(scheduler) {}
    SCHEDULER& scheduler;
};

template<typename SCHEDULER>
schedule_on_transform<SCHEDULER> schedule_on(SCHEDULER& scheduler) {
    return schedule_on_transform<SCHEDULER>{ scheduler };
}

template<typename T, typename SCHEDULER>
decltype(auto) operator|(T&& value, schedule_on_transform<SCHEDULER> transform) {
    return schedule_on(transform.scheduler, std::forward<T>(value));
}

template<typename SCHEDULER, typename AWAITABLE>
auto schedule_on(SCHEDULER& scheduler, AWAITABLE awaitable)
    -> task<detail::remove_rvalue_reference_t<typename detail::awaitable_traits<AWAITABLE>::await_result_t>> {
    co_await scheduler.schedule();
    co_return co_await std::move(awaitable);
}

template<typename T, typename SCHEDULER>
async_generator<T> schedule_on(SCHEDULER& scheduler, async_generator<T> source) {
    // Transfer exection to the scheduler before the implicit calls to
    // 'co_await begin()' or subsequent calls to `co_await iterator::operator++()`
    // below. This ensures that all calls to the generator's coroutine_handle<>::resume()
    // are executed on the execution context of the scheduler.
    co_await scheduler.schedule();

    const auto it_end = source.end();
    auto it = co_await source.begin();
    while (it != it_end) {
        co_yield *it;
        co_await scheduler.schedule();
        (void)co_await ++it;
    }
}

}

#endif //SCHEDULE_ON_H
