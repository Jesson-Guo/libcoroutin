//
// Created by Jesson on 2024/10/1.
//

#ifndef GET_AWAITER_H
#define GET_AWAITER_H

#include "any.h"
#include "is_awaiter.h"

namespace coro::detail {

// 调用成员函数 operator co_await()。int 参数用于重载优先级控制，使得这个重载在匹配优先级更高时被选中。
template<typename T>
auto get_awaiter_impl(T&& value, int)
    noexcept(noexcept(static_cast<T&&>(value).operator co_await()))
    -> decltype(static_cast<T&&>(value).operator co_await()) {
    return static_cast<T&&>(value).operator co_await();
}

// T 没有成员 operator co_await()，它会尝试查找全局作用域中的 operator co_await 函数。
// 这个重载通过 long 参数来降低优先级，当第一个重载不匹配时，它将被选中。
template<typename T>
auto get_awaiter_impl(T&& value, long)
    noexcept(noexcept(operator co_await(static_cast<T&&>(value))))
    -> decltype(operator co_await(static_cast<T&&>(value))) {
    return operator co_await(static_cast<T&&>(value));
}

// 如果 T 本身已经是一个合法的 awaiter。它通过 is_awaiter<T&&>::value 来检查 T 是否符合 awaiter 的要求。
// 如果符合，它直接返回这个对象本身作为 awaiter。
// any 是一个特殊类型，用于匹配所有其他情况
template<typename T, std::enable_if_t<is_awaiter<T&&>::value, int> = 0>
auto get_awaiter_impl(T&& value, any) noexcept -> T&& {
    return static_cast<T&&>(value);
}

// 封装了对 get_awaiter_impl 的调用，并且通过传递一个整型常量 123 来触发选择最合适的重载版本。
template<typename T>
auto get_awaiter(T&& value)
    noexcept(noexcept(detail::get_awaiter_impl(static_cast<T&&>(value), 123)))
    -> decltype(detail::get_awaiter_impl(static_cast<T&&>(value), 123)) {
    return detail::get_awaiter_impl(static_cast<T&&>(value), 123);
}

}

#endif //GET_AWAITER_H
