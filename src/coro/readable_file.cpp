//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/io/readable_file.h"

coro::file_read_operation coro::readable_file::read(std::uint64_t offset, void* buffer, std::size_t byte_count) const noexcept {
    return file_read_operation{*m_io_service, m_file_handle.fd(), offset, buffer, byte_count};
}

coro::file_read_operation_cancellable coro::readable_file::read(
    std::uint64_t offset, void* buffer, std::size_t byte_count, cancellation_token ct) const noexcept {
    return file_read_operation_cancellable{
        *m_io_service, m_file_handle.fd(), offset, buffer, byte_count, std::move(ct)
    };
}
