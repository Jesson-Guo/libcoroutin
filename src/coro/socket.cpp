//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/net/socket.h"

coro::net::socket_connect_operation
coro::net::socket::connect(const ip_endpoint& remote_endpoint) noexcept {
    return socket_connect_operation{m_io_queue, *this, remote_endpoint};
}

coro::net::socket_connect_operation_cancellable
coro::net::socket::connect(const ip_endpoint& remote_endpoint, cancellation_token ct) noexcept {
    return socket_connect_operation_cancellable{m_io_queue, *this, remote_endpoint, std::move(ct)};
}

coro::net::socket_accept_operation
coro::net::socket::accept(socket& accepting_socket) noexcept {
    return socket_accept_operation{m_io_queue, *this, accepting_socket};
}

coro::net::socket_accept_operation_cancellable
coro::net::socket::accept(socket& accepting_socket, cancellation_token ct) noexcept {
    return socket_accept_operation_cancellable{m_io_queue, *this, accepting_socket, std::move(ct)};
}

coro::net::socket_disconnect_operation
coro::net::socket::disconnect() noexcept {
    return socket_disconnect_operation{m_io_queue, *this};
}

coro::net::socket_disconnect_operation_cancellable
coro::net::socket::disconnect(cancellation_token ct) noexcept {
    return socket_disconnect_operation_cancellable{m_io_queue, *this, std::move(ct)};
}

coro::net::socket_send_operation
coro::net::socket::send(const void* buffer, std::size_t size) noexcept {
    return socket_send_operation{m_io_queue, *this, buffer, size};
}

coro::net::socket_send_operation_cancellable
coro::net::socket::send(const void* buffer, std::size_t size, cancellation_token ct) noexcept {
    return socket_send_operation_cancellable{m_io_queue, *this, buffer, size, std::move(ct)};
}

coro::net::socket_recv_operation
coro::net::socket::recv(void* buffer, std::size_t size) noexcept {
    return socket_recv_operation{m_io_queue, *this, buffer, size};
}

coro::net::socket_recv_operation_cancellable
coro::net::socket::recv(void* buffer, std::size_t size, cancellation_token ct) noexcept {
    return socket_recv_operation_cancellable{m_io_queue, *this, buffer, size, std::move(ct)};
}

coro::net::socket_recv_from_operation
coro::net::socket::recv_from(void* buffer, std::size_t size) noexcept {
    return socket_recv_from_operation{m_io_queue, *this, buffer, size};
}

coro::net::socket_recv_from_operation_cancellable
coro::net::socket::recv_from(void* buffer, std::size_t size, cancellation_token ct) noexcept {
    return socket_recv_from_operation_cancellable{m_io_queue, *this, buffer, size, std::move(ct)};
}

coro::net::socket_send_to_operation
coro::net::socket::send_to(const ip_endpoint& destination, const void* buffer, std::size_t size) noexcept {
    return socket_send_to_operation{m_io_queue, *this, destination, buffer, size};
}

coro::net::socket_send_to_operation_cancellable
coro::net::socket::send_to(const ip_endpoint& destination, const void* buffer, std::size_t size, cancellation_token ct) noexcept {
    return socket_send_to_operation_cancellable{m_io_queue, *this, destination, buffer, size, std::move(ct)};
}

