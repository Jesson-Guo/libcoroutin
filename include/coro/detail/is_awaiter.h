//
// Created by Jesson on 2024/10/1.
//

#ifndef IS_AWAITER_H
#define IS_AWAITER_H

#include <coroutine>
#include <type_traits>

namespace coro::detail {

template<typename T>
struct is_coroutine_handle : std::false_type {};

template<typename promise>
struct is_coroutine_handle<std::coroutine_handle<promise>> : std::true_type {};

template<typename T>
struct is_valid_await_suspend_return_value : std::disjunction<
    std::is_void<T>,
    std::is_same<T, bool>,
    is_coroutine_handle<T>> {};

template<typename T, typename = std::void_t<>>
struct is_awaiter : std::false_type {};

template<typename T>
struct is_awaiter<T, std::void_t<
    decltype(std::declval<T>().await_ready()),
    decltype(std::declval<T>().await_suspend(std::declval<std::coroutine_handle<>>())),
    decltype(std::declval<T>().await_resume())>> :
    std::conjunction<
        std::is_constructible<bool, decltype(std::declval<T>().await_ready())>,
        is_valid_await_suspend_return_value<
            decltype(std::declval<T>().await_suspend(std::declval<std::coroutine_handle<>>()))>> {};

}

#endif //IS_AWAITER_H
