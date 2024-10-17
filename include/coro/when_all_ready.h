//
// Created by Jesson on 2024/10/18.
//

#ifndef WHEN_ALL_READY_H
#define WHEN_ALL_READY_H

#include "awaitable_traits.h"
#include "is_awaitable.h"

#include "detail/when_all_ready_awaitable.h"
#include "detail/when_all_task.h"
#include "detail/unwrap_reference.h"

#include <tuple>
#include <utility>
#include <vector>
#include <type_traits>

namespace coro {

template<
    typename... AWAITABLES,
    std::enable_if_t<std::conjunction_v<
        is_awaitable<detail::unwrap_reference_t<std::remove_reference_t<AWAITABLES>>>...>, int> = 0>
[[nodiscard]] auto when_all_ready(AWAITABLES&&... awaitables) {
    return detail::when_all_ready_awaitable<std::tuple<detail::when_all_task<
        typename detail::awaitable_traits<detail::unwrap_reference_t<std::remove_reference_t<AWAITABLES>>>::await_result_t>...>>(
            std::make_tuple(detail::make_when_all_task(std::forward<AWAITABLES>(awaitables))...));
}

template<
    typename AWAITABLE,
    typename RESULT = typename detail::awaitable_traits<detail::unwrap_reference_t<AWAITABLE>>::await_result_t>
[[nodiscard]] auto when_all_ready(std::vector<AWAITABLE> awaitables) {
    std::vector<detail::when_all_task<RESULT>> tasks;

    tasks.reserve(awaitables.size());

    for (auto& awaitable : awaitables) {
        tasks.emplace_back(detail::make_when_all_task(std::move(awaitable)));
    }

    return detail::when_all_ready_awaitable<std::vector<detail::when_all_task<RESULT>>>(std::move(tasks));
}

// TODO: Generalise this from vector<AWAITABLE> to arbitrary sequence of awaitable.
template<
    typename TASK_CONTAINER,
    typename AWAITABLE = typename TASK_CONTAINER::value_type,
    typename RESULT = typename detail::awaitable_traits<detail::unwrap_reference_t<AWAITABLE>>::await_result_t>
[[nodiscard]] auto when_all_ready(TASK_CONTAINER awaitables) {
    TASK_CONTAINER<detail::when_all_task<RESULT>> tasks;

    tasks.reserve(awaitables.size());

    for (auto& awaitable : awaitables) {
        tasks.emplace_back(detail::make_when_all_task(std::move(awaitable)));
    }

    return detail::when_all_ready_awaitable<std::vector<detail::when_all_task<RESULT>>>(std::move(tasks));
}

}

#endif //WHEN_ALL_READY_H
