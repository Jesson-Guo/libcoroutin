//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/io/read_only_file.h"

coro::read_only_file coro::read_only_file::open(
        io_service& io_svc,
        const std::filesystem::path& path,
        file_share_mode share_mode,
        file_buffering_mode buffering_mode) {
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

coro::read_only_file::read_only_file(detail::macos::safe_fd&& file_handle) noexcept
    : file(std::move(file_handle))
    , readable_file(detail::macos::safe_fd{}) {}
