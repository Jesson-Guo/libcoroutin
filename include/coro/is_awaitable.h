//
// Created by Jesson on 2024/10/18.
//

#ifndef IS_AWAITABLE_H
#define IS_AWAITABLE_H

#include "detail/get_awaiter.h"

namespace coro {

template<typename T, typename = std::void_t<>>
struct is_awaitable : std::false_type {};

template<typename T>
struct is_awaitable<T, std::void_t<decltype(detail::get_awaiter(std::declval<T>()))>> : std::true_type {};

template<typename T>
constexpr bool is_awaitable_v = is_awaitable<T>::value;

}

#endif //IS_AWAITABLE_H
