//
// Created by Jesson on 2024/10/12.
//

#ifndef SOCKET_SEND_OPERATION_H
#define SOCKET_SEND_OPERATION_H

#include "../cancellation/cancellation_token.h"
#include "../detail/macos_io_operation.h"

namespace coro::net {

class socket;
class socket_send_operation_impl {
public:
    socket_send_operation_impl(socket& s, const void* buffer, const std::size_t size) noexcept
        : m_socket(s)
        , m_buffer(const_cast<void*>(buffer))
        , m_byte_count(size) {}

    bool try_start(coro::detail::io_operation_base& operation) const noexcept;
    void cancel(coro::detail::io_operation_base& operation) noexcept;

private:
    socket& m_socket;
    void* m_buffer;
    std::size_t m_byte_count;
};

class socket_send_operation : public coro::detail::io_operation<socket_send_operation> {
public:
    socket_send_operation(coro::detail::macos::io_queue& io_queue, socket& s, const void* buffer, std::size_t size) noexcept
        : io_operation{io_queue}
        , m_send_op_impl(s, buffer, size) {}

private:
    friend io_operation;

    bool try_start() noexcept { return m_send_op_impl.try_start(*this); }

    socket_send_operation_impl m_send_op_impl;
};

class socket_send_operation_cancellable : public coro::detail::io_operation_cancellable<socket_send_operation_cancellable> {
public:
    socket_send_operation_cancellable(
        coro::detail::macos::io_queue& io_queue,socket& s, const void* buffer, std::size_t size, cancellation_token&& ct) noexcept
        : io_operation_cancellable {io_queue, std::move(ct)}
        , m_send_op_impl(s, buffer, size) {}

private:
    friend io_operation_cancellable;

    bool try_start() noexcept { return m_send_op_impl.try_start(*this); }
    void cancel() noexcept { return m_send_op_impl.cancel(*this); }

    socket_send_operation_impl m_send_op_impl;
};

}

#endif //SOCKET_SEND_OPERATION_H
