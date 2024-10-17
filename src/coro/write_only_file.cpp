//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/io/write_only_file.h"

coro::write_only_file coro::write_only_file::open(
        io_service& io_svc,
        const std::filesystem::path& path,
        file_open_mode open_mode,
        file_share_mode share_mode,
        file_buffering_mode buffering_mode) {
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

coro::write_only_file::write_only_file(detail::macos::safe_fd&& file_handle) noexcept
    : file(std::move(file_handle))
    , writable_file(detail::macos::safe_fd{}) {}
