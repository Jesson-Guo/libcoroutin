//
// Created by Jesson on 2024/10/18.
//

#ifndef UNWRAP_REFERENCE_H
#define UNWRAP_REFERENCE_H

#include <functional>

namespace coro::detail {

template<typename T>
struct unwrap_reference {
    using type = T;
};

template<typename T>
struct unwrap_reference<std::reference_wrapper<T>> {
    using type = T;
};

template<typename T>
using unwrap_reference_t = typename unwrap_reference<T>::type;

}

#endif //UNWRAP_REFERENCE_H
