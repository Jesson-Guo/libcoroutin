//
// Created by Jesson on 2024/10/10.
//

#ifndef FILE_READ_OPERATION_H
#define FILE_READ_OPERATION_H

#include "../cancellation/cancellation_token.h"
#include "../detail/macos.h"
#include "../detail/macos_io_operation.h"
#include "io_service.h"

#include <unistd.h>

namespace coro {

class file_read_operation_impl {
public:
    using fd_t = detail::macos::fd_t;
    
    file_read_operation_impl(fd_t file_handle, void* buffer, std::size_t byte_count) noexcept
        : m_file_handle(file_handle)
        , m_buffer(buffer)
        , m_byte_count(byte_count) {}

    bool try_start(detail::io_operation_base& operation, std::uint64_t offset) const noexcept;
    void cancel(detail::io_operation_base& operation) noexcept;

private:
    fd_t m_file_handle;
    void* m_buffer;
    std::size_t m_byte_count;
};

class file_read_operation : public detail::io_operation<file_read_operation> {
public:
    using fd_t = detail::macos::fd_t;
    
    file_read_operation(io_service& io_svc, fd_t file_handle, std::uint64_t offset, void* buffer, std::size_t byte_count) noexcept
        : io_operation(io_svc.io_queue())
        , m_file_read_impl(file_handle, buffer, byte_count)
        , m_offset(offset) {}

private:
    friend io_operation;

    bool try_start() noexcept { return m_file_read_impl.try_start(*this, m_offset); }

    file_read_operation_impl m_file_read_impl;
    std::uint64_t m_offset;
};

class file_read_operation_cancellable : public detail::io_operation_cancellable<file_read_operation_cancellable> {
public:
    using fd_t = detail::macos::fd_t;
    
    file_read_operation_cancellable(
        io_service& io_svc, fd_t file_handle, std::uint64_t offset, void* buffer, std::size_t byte_count, cancellation_token&& token) noexcept
        : io_operation_cancellable(io_svc.io_queue(), std::move(token))
        , m_file_read_impl(file_handle, buffer, byte_count)
        , m_offset(offset) {}

private:

    friend io_operation_cancellable;

    bool try_start() noexcept { return m_file_read_impl.try_start(*this, m_offset); }
    void cancel() noexcept { m_file_read_impl.cancel(*this); }

    file_read_operation_impl m_file_read_impl;
    std::uint64_t m_offset;
};

}

#endif //FILE_READ_OPERATION_H
