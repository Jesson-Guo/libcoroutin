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
    typename... awaitable_type,
    std::enable_if_t<std::conjunction_v<
        is_awaitable<detail::unwrap_reference_t<std::remove_reference_t<awaitable_type>>>...>, int> = 0>
[[nodiscard]] auto when_all_ready(awaitable_type&&... awaitables) {
    return detail::when_all_ready_awaitable<std::tuple<detail::when_all_task<
        typename detail::awaitable_traits<detail::unwrap_reference_t<std::remove_reference_t<awaitable_type>>>::await_result_t>...>>(
            std::make_tuple(detail::make_when_all_task(std::forward<awaitable_type>(awaitables))...));
}

template<
    typename awaitable_type,
    typename result = typename detail::awaitable_traits<detail::unwrap_reference_t<awaitable_type>>::await_result_t>
[[nodiscard]] auto when_all_ready(std::vector<awaitable_type> awaitables) {
    std::vector<detail::when_all_task<result>> tasks;

    tasks.reserve(awaitables.size());

    for (auto& awaitable : awaitables) {
        tasks.emplace_back(detail::make_when_all_task(std::move(awaitable)));
    }

    return detail::when_all_ready_awaitable<std::vector<detail::when_all_task<result>>>(std::move(tasks));
}

// TODO: Generalise this from vector<awaitable_type> to arbitrary sequence of awaitable.
// template<
//     typename task_container,
//     typename awaitable_type = typename task_container::value_type,
//     typename result = typename detail::awaitable_traits<detail::unwrap_reference_t<awaitable_type>>::await_result_t>
// [[nodiscard]] auto when_all_ready(task_container awaitables) {
//     std::vector<detail::when_all_task<result>> tasks;
//
//     tasks.reserve(awaitables.size());
//
//     for (auto& awaitable : awaitables) {
//         tasks.emplace_back(detail::make_when_all_task(std::move(awaitable)));
//     }
//
//     return detail::when_all_ready_awaitable<std::vector<detail::when_all_task<result>>>(std::move(tasks));
// }

}

#endif //WHEN_ALL_READY_H
