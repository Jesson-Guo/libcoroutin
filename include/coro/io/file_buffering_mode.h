//
// Created by Jesson on 2024/10/9.
//

#ifndef FILE_BUFFERING_MODE_H
#define FILE_BUFFERING_MODE_H

namespace coro {

enum class file_buffering_mode {
    default_ = 0,
    sequential = 1,
    random_access = 2,
    unbuffered = 4,
    write_through = 8,
    temporary = 16
};

constexpr file_buffering_mode operator&(file_buffering_mode a, file_buffering_mode b) {
    return static_cast<file_buffering_mode>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr file_buffering_mode operator|(file_buffering_mode a, file_buffering_mode b) {
    return static_cast<file_buffering_mode>(static_cast<int>(a) | static_cast<int>(b));
}

}

#endif //FILE_BUFFERING_MODE_H
