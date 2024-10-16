//
// Created by Jesson on 2024/10/16.
//

#ifndef WRITE_ONLY_FILE_H
#define WRITE_ONLY_FILE_H

#include "writable_file.h"
#include "file_share_mode.h"
#include "file_buffering_mode.h"
#include "file_open_mode.h"

#include <filesystem>

#define GENERIC_WRITE (S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH)

namespace coro {

class write_only_file : public writable_file {
public:
    /// Open a file for write-only access.
    ///
    /// \param io_svc
    /// The I/O context to use when dispatching I/O completion events.
    /// When asynchronous write operations on this file complete the
    /// completion events will be dispatched to an I/O thread associated
    /// with the I/O context.
    ///
    /// \param path
    /// Path of the file to open.
    ///
    /// \param open_mode
    /// Specifies how the file should be opened and how to handle cases
    /// when the file exists or doesn't exist.
    ///
    /// \param share_mode
    /// Specifies the access to be allowed on the file concurrently with this file access.
    ///
    /// \param buffering_mode
    /// Specifies the modes/hints to provide to the OS that affects the behaviour
    /// of its file buffering.
    ///
    /// \return
    /// An object that can be used to write to the file.
    ///
    /// \throw std::system_error
    /// If the file could not be opened for write.
    [[nodiscard]]
    static write_only_file open(
        io_service& io_svc,
        const std::filesystem::path& path,
        file_open_mode open_mode = file_open_mode::create_or_open,
        file_share_mode share_mode = file_share_mode::none,
        file_buffering_mode buffering_mode = file_buffering_mode::default_) {
        auto file = write_only_file(file::open(
            GENERIC_WRITE,
            io_svc,
            path,
            open_mode,
            share_mode,
            buffering_mode));
        file.m_io_service = &io_svc;
        return std::move(file);
    }

protected:
    explicit write_only_file(detail::macos::safe_fd&& file_handle) noexcept
        : file(std::move(file_handle))
        , writable_file(detail::macos::safe_fd{}) {}
};

}

#endif //WRITE_ONLY_FILE_H
