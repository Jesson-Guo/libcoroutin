//
// Created by Jesson on 2024/10/9.
//

#ifndef FILE_SHARE_MODE_H
#define FILE_SHARE_MODE_H

namespace coro {

enum class file_share_mode {
    /// Don't allow any other processes to open the file concurrently.
    none = 0,

    /// Allow other processes to open the file in read-only mode
    /// concurrently with this process opening the file.
    read = 1,

    /// Allow other processes to open the file in write-only mode
    /// concurrently with this process opening the file.
    write = 2,

    /// Allow other processes to open the file in read and/or write mode
    /// concurrently with this process opening the file.
    read_write = read | write,

    /// Allow other processes to delete the file while this process
    /// has the file open.
    delete_ = 4
};

constexpr file_share_mode operator|(file_share_mode a, file_share_mode b) {
    return static_cast<file_share_mode>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr file_share_mode operator&(file_share_mode a, file_share_mode b) {
    return static_cast<file_share_mode>(static_cast<int>(a) & static_cast<int>(b));
}

}

#endif //FILE_SHARE_MODE_H
