//
// Created by Jesson on 2024/10/17.
//

#ifndef REMOVE_RVALUE_REFERENCE_H
#define REMOVE_RVALUE_REFERENCE_H

namespace coro::detail {

template<typename T>
struct remove_rvalue_reference {
    using type = T;
};

template<typename T>
struct remove_rvalue_reference<T&&> {
    using type = T;
};

template<typename T>
using remove_rvalue_reference_t = typename remove_rvalue_reference<T>::type;

}

#endif //REMOVE_RVALUE_REFERENCE_H
