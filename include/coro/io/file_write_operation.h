//
// Created by Jesson on 2024/10/15.
//

#ifndef FILE_WRITE_OPERATION_H
#define FILE_WRITE_OPERATION_H

#include "../cancellation/cancellation_token.h"
#include "../detail/macos.h"
#include "../detail/macos_io_operation.h"
#include "io_service.h"

namespace coro {

class file_write_operation_impl {
public:
    using fd_t = detail::macos::fd_t;
    
    file_write_operation_impl(fd_t file_handle, const void* buffer, std::size_t byte_count) noexcept
        : m_file_handle(file_handle)
        , m_buffer(buffer)
        , m_byte_count(byte_count) {}

    bool try_start(detail::io_operation_base& operation, std::uint64_t offset) noexcept;
    void cancel(detail::io_operation_base& operation) noexcept;

private:
    fd_t m_file_handle;
    const void* m_buffer;
    std::size_t m_byte_count;
};

class file_write_operation : public detail::io_operation<file_write_operation> {
public:
    using fd_t = detail::macos::fd_t;
    
    file_write_operation(io_service& io_svc, fd_t file_handle, std::uint64_t offset, const void* buffer, std::size_t byte_count) noexcept
        : io_operation(io_svc.io_queue())
        , m_file_write_impl(file_handle, buffer, byte_count)
        , m_offset(offset) {}

private:
    friend io_operation;

    bool try_start() noexcept { return m_file_write_impl.try_start(*this, m_offset); }

    file_write_operation_impl m_file_write_impl;
    std::uint64_t m_offset;
};

class file_write_operation_cancellable : public detail::io_operation_cancellable<file_write_operation_cancellable> {
public:
    using fd_t = detail::macos::fd_t;
    
    file_write_operation_cancellable(
        io_service& io_svc, fd_t file_handle, std::uint64_t offset, const void* buffer, std::size_t byte_count, cancellation_token&& ct) noexcept
        : io_operation_cancellable(io_svc.io_queue(), std::move(ct))
        , m_file_write_impl(file_handle, buffer, byte_count)
        , m_offset(offset) {}

private:
    friend io_operation_cancellable;

    bool try_start() noexcept { return m_file_write_impl.try_start(*this, m_offset); }
    void cancel() noexcept { m_file_write_impl.cancel(*this); }

    file_write_operation_impl m_file_write_impl;
    std::uint64_t m_offset;
};

}

bool coro::file_write_operation_impl::try_start(detail::io_operation_base& operation, std::uint64_t offset) noexcept {
    const size_t bytes = m_byte_count <= std::numeric_limits<size_t>::max() ? m_byte_count : std::numeric_limits<size_t>::max();
    return operation.m_io_queue.transaction(operation.m_message)
        .pwrite(m_file_handle, m_buffer, bytes, offset)
        .commit();
}

void coro::file_write_operation_impl::cancel(detail::io_operation_base& operation) noexcept {
    operation.m_io_queue.transaction(operation.m_message).cancel().commit();
}


#endif //FILE_WRITE_OPERATION_H
