//
// Created by Jesson on 2024/10/11.
//

#ifndef OPERATION_CANCELLED_H
#define OPERATION_CANCELLED_H

#include <exception>

namespace coro {

class operation_cancelled final : public std::exception {
public:
    operation_cancelled() noexcept = default;
    const char* what() const noexcept override { return "operation cancelled"; }
};

}

#endif //OPERATION_CANCELLED_H
