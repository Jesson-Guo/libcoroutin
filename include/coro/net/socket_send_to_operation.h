//
// Created by Jesson on 2024/10/12.
//

#ifndef SOCKET_SEND_TO_OPERATION_H
#define SOCKET_SEND_TO_OPERATION_H

#include "socket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <system_error>

namespace coro::net {

class socket;
class socket_send_to_operation_impl {
public:
    socket_send_to_operation_impl(socket& s, const ip_endpoint& destination, const void* buffer, std::size_t byte_count) noexcept
        : m_socket(s)
        , m_destination(destination)
        , m_buffer(buffer)
        , m_byte_count(byte_count) {}

    bool try_start(coro::detail::io_operation_base& operation) noexcept;
    void cancel(coro::detail::io_operation_base& operation) noexcept;

private:
    socket& m_socket;
    ip_endpoint m_destination;
    const void* m_buffer;
    std::size_t m_byte_count;
};

class socket_send_to_operation : public coro::detail::io_operation<socket_send_to_operation> {
public:
    socket_send_to_operation(
        coro::detail::macos::io_queue& io_queue, socket& s, const ip_endpoint& destination, const void* buffer, std::size_t byte_count) noexcept
        : io_operation {io_queue}
        , m_impl(s, destination, buffer, byte_count) {}

private:
    friend io_operation;

    bool try_start() noexcept { return m_impl.try_start(*this); }

    socket_send_to_operation_impl m_impl;
};

class socket_send_to_operation_cancellable : public coro::detail::io_operation_cancellable<socket_send_to_operation_cancellable> {
public:
    socket_send_to_operation_cancellable(
        coro::detail::macos::io_queue& io_queue, socket& s, const ip_endpoint& destination, const void* buffer, std::size_t byte_count, cancellation_token&& ct) noexcept
        : io_operation_cancellable {io_queue, std::move(ct)}
        , m_impl(s, destination, buffer, byte_count) {}

private:
    friend io_operation_cancellable;

    bool try_start() noexcept { return m_impl.try_start(*this); }
    void cancel() noexcept { m_impl.cancel(*this); }

    socket_send_to_operation_impl m_impl;
};

}

#endif //SOCKET_SEND_TO_OPERATION_H
