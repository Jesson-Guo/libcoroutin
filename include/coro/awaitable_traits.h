//
// Created by Jesson on 2024/10/2.
//

#ifndef AWAITABLE_TRAITS_H
#define AWAITABLE_TRAITS_H

#include "detail/get_awaiter.h"

#include <type_traits>

namespace coro::detail {

template<typename T, typename = void>
struct awaitable_traits {};

template<typename T>
struct awaitable_traits<T, std::void_t<decltype(get_awaiter(std::declval<T>()))>> {
    using awaiter_t = decltype(get_awaiter(std::declval<T>()));
    using await_result_t = decltype(std::declval<awaiter_t>().await_resume());
};

}

#endif //AWAITABLE_TRAITS_H
