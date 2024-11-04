//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/net/socket.h"

void coro::net::socket::bind(const ip_endpoint& local_endpoint) {
    SOCKADDR_STORAGE sockaddr_storage = { 0 };
    auto* sockaddr = reinterpret_cast<SOCKADDR*>(&sockaddr_storage);
    socklen_t sockaddr_len = 0;
    if (local_endpoint.is_ipv4()) {
        SOCKADDR_IN& ipv4_sockaddr = *reinterpret_cast<SOCKADDR_IN*>(sockaddr);
        ipv4_sockaddr.sin_family = AF_INET;
        std::memcpy(&ipv4_sockaddr.sin_addr, local_endpoint.to_ipv4().address().bytes(), 4);
        ipv4_sockaddr.sin_port = htons(local_endpoint.to_ipv4().port());
        sockaddr_len = sizeof(SOCKADDR_IN);
    }
    else {
        SOCKADDR_IN6& ipv6_sockaddr = *reinterpret_cast<SOCKADDR_IN6*>(sockaddr);
        ipv6_sockaddr.sin6_family = AF_INET6;
        std::memcpy(&ipv6_sockaddr.sin6_addr, local_endpoint.to_ipv6().address().bytes(), 16);
        ipv6_sockaddr.sin6_port = htons(local_endpoint.to_ipv6().port());
        sockaddr_len = sizeof(SOCKADDR_IN6);
    }

    int result = ::bind(m_fd, sockaddr, sockaddr_len);
    if (result != 0) {
        throw std::system_error(last_error, std::system_category(), "Error binding to endpoint: bind()");
    }

    SOCKADDR_STORAGE bound_sockaddr_storage = {};
    auto* bound_sockaddr = reinterpret_cast<SOCKADDR*>(&bound_sockaddr_storage);
    socklen_t bound_sockaddr_len = sizeof(bound_sockaddr_storage);
    result = getsockname(m_fd, bound_sockaddr, &bound_sockaddr_len);
    if (result == 0) {
        m_local_endpoint = detail::sockaddr_to_ip_endpoint(*bound_sockaddr);
    }
    else {
        m_local_endpoint = local_endpoint;
    }
}

void coro::net::socket::listen() const {
    if (const int result = ::listen(m_fd, SOMAXCONN); result != 0) {
        throw std::system_error(
            last_error,
            std::system_category(),
            "Failed to start listening on bound endpoint: listen");
    }
}

void coro::net::socket::listen(std::uint32_t backlog) const {
    if (backlog > 0x7FFFFFFF) {
        backlog = 0x7FFFFFFF;
    }

    if (const int result = ::listen(m_fd, static_cast<int>(backlog)); result != 0) {
        throw std::system_error(
            last_error,
            std::system_category(),
            "Failed to start listening on bound endpoint: listen");
    }
}

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

void coro::net::socket::close_send() const {
    if (const int result = shutdown(m_fd, SHUT_WR); result == -1) {
        throw std::system_error(
            last_error,
            std::system_category(),
            "Failed to close socket send stream: shutdown(SHUT_WR)");
    }
}

void coro::net::socket::close_recv() const {
    if (const int result = shutdown(m_fd, SHUT_RD); result == -1) {
        throw std::system_error(
            last_error,
            std::system_category(),
            "Failed to close socket receive stream: shutdown(SHUT_RD)");
    }
}
