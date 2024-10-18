//
// Created by Jesson on 2024/10/2.
//

#ifndef FILE_H
#define FILE_H

#include "file_buffering_mode.h"
#include "file_open_mode.h"
#include "file_share_mode.h"
#include "io_service.h"
#include "../detail/macos.h"

#include <filesystem>

namespace coro {

class io_service;

class file {
public:
    using safe_fd = detail::macos::safe_fd;

    file(file&& other) noexcept = default;

    virtual ~file();

    /// Get the size of the file in bytes.
    std::uint64_t size() const;

protected:
    explicit file(safe_fd&& file_handle) noexcept;

    static safe_fd open(
        int fill_access,
        io_service& io_svc,
        const std::filesystem::path& path,
        file_open_mode open_mode,
        file_share_mode share_mode,
        file_buffering_mode buffering_mode);

    safe_fd m_file_handle;
    io_service* m_io_service;
};

}

#endif //FILE_H
