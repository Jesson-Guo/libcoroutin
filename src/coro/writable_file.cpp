//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/io/writable_file.h"

void coro::writable_file::set_size(off_t file_size) const {
    if (ftruncate(m_file_handle.fd(), file_size) < 0) {
        throw std::system_error{
            errno, std::system_category(), "error setting file size: ftruncate"
        };
    }
}

coro::file_write_operation coro::writable_file::write(std::uint64_t offset, void* buffer, std::size_t byte_count) const noexcept {
    return file_write_operation{
        *m_io_service, m_file_handle.fd(), offset, buffer, byte_count
    };
}

coro::file_write_operation_cancellable coro::writable_file::write(
        std::uint64_t offset, void* buffer, std::size_t byte_count, cancellation_token ct) const noexcept {
    return file_write_operation_cancellable{
        *m_io_service, m_file_handle.fd(), offset, buffer, byte_count, std::move(ct)
    };
}
