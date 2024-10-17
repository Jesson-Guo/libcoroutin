//
// Created by Jesson on 2024/10/18.
//

#ifndef WHEN_ALL_H
#define WHEN_ALL_H

#include "when_all_ready.h"
#include "awaitable_traits.h"
#include "is_awaitable.h"
#include "fmap.h"

#include "detail/unwrap_reference.h"

#include <tuple>
#include <functional>
#include <vector>

namespace coro {

//////////
// Variadic when_all()
template<
	typename... AWAITABLES,
	std::enable_if_t<
		std::conjunction_v<is_awaitable<detail::unwrap_reference_t<std::remove_reference_t<AWAITABLES>>>...>,
		int> = 0>
[[nodiscard]] auto when_all(AWAITABLES&&... awaitables) {
	return fmap([]<typename T0>(T0&& task_tuple) {
		return std::apply([]<typename... T1>(T1&&... tasks) {
			return std::make_tuple(static_cast<T0>(tasks).non_void_result()...);
		}, static_cast<T0>(task_tuple));
	}, when_all_ready(std::forward<AWAITABLES>(awaitables)...));
}

//////////
// when_all() with vector of awaitable
template<
	typename AWAITABLE,
	typename RESULT = typename detail::awaitable_traits<detail::unwrap_reference_t<AWAITABLE>>::await_result_t,
	std::enable_if_t<std::is_void_v<RESULT>, int> = 0>
[[nodiscard]] auto when_all(std::vector<AWAITABLE> awaitables) {
	return fmap([](auto&& task_vec) {
		for (auto& task : task_vec) {
			task.result();
		}
	}, when_all_ready(std::move(awaitables)));
}

template<
	typename AWAITABLE,
	typename RESULT = typename detail::awaitable_traits<detail::unwrap_reference_t<AWAITABLE>>::await_result_t,
	std::enable_if_t<!std::is_void_v<RESULT>, int> = 0>
[[nodiscard]] auto when_all(std::vector<AWAITABLE> awaitables) {
	using result_t = std::conditional_t<
		std::is_lvalue_reference_v<RESULT>,
		std::reference_wrapper<std::remove_reference_t<RESULT>>,
		std::remove_reference_t<RESULT>>;

	return fmap([]<typename T0>(T0&& task_vec) {
		std::vector<result_t> results;
		results.reserve(task_vec.size());
		for (auto& task : task_vec) {
			if constexpr (std::is_rvalue_reference_v<T0>) {
				results.emplace_back(std::move(task).result());
			}
			else {
				results.emplace_back(task.result());
			}
		}
		return results;
	}, when_all_ready(std::move(awaitables)));
}

}

#endif //WHEN_ALL_H
