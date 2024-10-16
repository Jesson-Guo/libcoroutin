//
// Created by Jesson on 2024/10/9.
//

#ifndef FILE_OPEN_MODE_H
#define FILE_OPEN_MODE_H

namespace coro {

enum class file_open_mode {
    /// Open an existing file.
    ///
    /// If file does not already exist when opening the file then raises
    /// an exception.
    open_existing,

    /// Create a new file, overwriting an existing file if one exists.
    ///
    /// If a file exists at the path then it is overwitten with a new file.
    /// If no file exists at the path then a new one is created.
    create_always,

    /// Create a new file.
    ///
    /// If the file already exists then raises an exception.
    create_new,

    /// Open the existing file if one exists, otherwise create a new empty file.
    create_or_open,

    /// Open the existing file, truncating the file size to zero.
    ///
    /// If the file does not exist then raises an exception.
    truncate_existing
};

}

#endif //FILE_OPEN_MODE_H
