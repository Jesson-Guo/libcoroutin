//
// Created by Jesson on 2024/10/3.
//

#ifndef OPERATION_CANCELLED_H
#define OPERATION_CANCELLED_H

#include <exception>

namespace coro {

class operation_cancelled : public std::exception {
public:
    operation_cancelled() noexcept = default;

    /// std::exception 类的一个虚函数，重写以返回异常的描述信息。
    const char* what() const noexcept override {
        return "operation cancelled";
    }
};

}

#endif //OPERATION_CANCELLED_H
