//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/io/read_write_file.h"

coro::read_write_file coro::read_write_file::open(
        io_service& io_svc,
        const std::filesystem::path& path,
        file_open_mode open_mode,
        file_share_mode share_mode,
        file_buffering_mode buffering_mode) {
    auto f = read_write_file(file::open(
        GENERIC_READ | GENERIC_WRITE,
        io_svc,
        path,
        open_mode,
        share_mode,
        buffering_mode));
    f.m_io_service = &io_svc;
    return std::move(f);
}

coro::read_write_file::read_write_file(detail::macos::safe_fd&& file_handle) noexcept
    : file(std::move(file_handle))
    , readable_file(detail::macos::safe_fd{})
    , writable_file(detail::macos::safe_fd{}) {}
