//
// Created by Jesson on 2024/10/18.
//

#ifndef BROKEN_PROMISE_H
#define BROKEN_PROMISE_H

#include <stdexcept>

namespace coro {

/// \brief
/// Exception thrown when you attempt to retrieve the result of
/// a task that has been detached from its promise/coroutine.
class broken_promise : public std::logic_error {
public:
    broken_promise() : std::logic_error("broken promise") {}
};

}

#endif //BROKEN_PROMISE_H
