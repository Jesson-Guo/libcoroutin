//
// Created by Jesson on 2024/10/1.
//

#ifndef SEQUENCE_TRAITS_H
#define SEQUENCE_TRAITS_H

#include <type_traits>

namespace coro {

template<typename T>
struct sequence_traits {
    using value_type = T;
    using difference_type = std::make_signed_t<T>;
    using size_type = std::make_unsigned_t<T>;

    static constexpr value_type initial_sequence = static_cast<value_type>(-1);

    static constexpr difference_type difference(value_type a, value_type b) {
        return static_cast<difference_type>(a - b);
    }

    static constexpr bool precedes(value_type a, value_type b) {
        return difference(a, b) < 0;
    }
};

}

#endif //SEQUENCE_TRAITS_H
