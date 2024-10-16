//
// Created by Jesson on 2024/10/16.
//

#ifndef READ_ONLY_FILE_H
#define READ_ONLY_FILE_H

#include "readable_file.h"
#include "file_share_mode.h"
#include "file_buffering_mode.h"

#include <filesystem>

#define GENERIC_READ (S_IRUSR | S_IRGRP | S_IROTH)

namespace coro {

class read_only_file : public readable_file {
public:
    /// Open a file for read-only access.
    ///
    /// \param ioContext
    /// The I/O context to use when dispatching I/O completion events.
    /// When asynchronous read operations on this file complete the
    /// completion events will be dispatched to an I/O thread associated
    /// with the I/O context.
    ///
    /// \param path
    /// Path of the file to open.
    ///
    /// \param shareMode
    /// Specifies the access to be allowed on the file concurrently with this file access.
    ///
    /// \param bufferingMode
    /// Specifies the modes/hints to provide to the OS that affects the behaviour
    /// of its file buffering.
    ///
    /// \return
    /// An object that can be used to read from the file.
    ///
    /// \throw std::system_error
    /// If the file could not be opened for read.
    [[nodiscard]] static read_only_file open(
        io_service& io_svc,
        const std::filesystem::path& path,
        file_share_mode share_mode = file_share_mode::read,
        file_buffering_mode buffering_mode = file_buffering_mode::default_) {
        read_only_file file(file::open(
            GENERIC_READ,
            io_svc,
            path,
            file_open_mode::open_existing,
            share_mode,
            buffering_mode));
        file.m_io_service = &io_svc;
        return std::move(file);
    }

protected:
    explicit read_only_file(detail::macos::safe_fd&& file_handle) noexcept
        : file(std::move(file_handle))
        , readable_file(detail::macos::safe_fd{}) {}
};

}


#endif //READ_ONLY_FILE_H
