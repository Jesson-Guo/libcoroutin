//
// Created by Jesson on 2024/10/1.
//

#ifndef ANY_H
#define ANY_H

namespace coro::detail {

// Helper type that can be cast-to from any type.
struct any {
    template<typename T>
    any(T&&) noexcept {}
};

}

#endif //ANY_H
