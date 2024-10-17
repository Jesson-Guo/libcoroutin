//
// Created by Jesson on 2024/10/11.
//

#ifndef SOCKET_H
#define SOCKET_H

#include "../detail/macos.h"
#include "../detail/macos_io_operation.h"

#include "../cancellation/cancellation_token.h"
#include "../io/io_service.h"

#include "ip_endpoint.h"
#include "socket_accept_operation.h"
#include "socket_connect_operation.h"
#include "socket_disconnect_operation.h"
#include "socket_helpers.h"
#include "socket_recv_from_operation.h"
#include "socket_recv_operation.h"
#include "socket_send_operation.h"
#include "socket_send_to_operation.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define last_error errno

namespace coro {

class io_service;

namespace net {

class socket {
public:
    /// Create a socket that can be used to communicate using TCP/IPv4 protocol.
    static socket create_tcpv4(io_service& io_svc) {
        auto socket_handle = create_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        socket result(io_svc.io_queue(), socket_handle);
        result.m_local_endpoint = ipv4_endpoint();
        result.m_remote_endpoint = ipv4_endpoint();
        return result;
    }

    /// Create a socket that can be used to communicate using TCP/IPv6 protocol.
    static socket create_tcpv6(io_service& io_svc) {
        auto socket_handle = create_socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        socket result(io_svc.io_queue(), socket_handle);
        result.m_local_endpoint = ipv6_endpoint();
        result.m_remote_endpoint = ipv6_endpoint();
        return result;
    }

    /// Create a socket that can be used to communicate using UDP/IPv4 protocol.
    static socket create_udpv4(io_service& io_svc) {
        auto socket_handle = create_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        socket result(io_svc.io_queue(), socket_handle);
        result.m_recv_flags = MSG_TRUNC;
        result.m_local_endpoint = ipv4_endpoint();
        result.m_remote_endpoint = ipv4_endpoint();
        return result;
    }

    /// Create a socket that can be used to communicate using UDP/IPv6 protocol.
    static socket create_udpv6(io_service& io_svc) {
        auto socket_handle = create_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
        socket result(io_svc.io_queue(), socket_handle);
        result.m_recv_flags = MSG_TRUNC;
        result.m_local_endpoint = ipv6_endpoint();
        result.m_remote_endpoint = ipv6_endpoint();
        return result;
    }

    socket(socket&& other) noexcept
        : m_handle(std::exchange(other.m_handle, -1))
        , m_io_queue(other.m_io_queue)
        , m_recv_flags(other.m_recv_flags)
        , m_local_endpoint(other.m_local_endpoint)
        , m_remote_endpoint(other.m_remote_endpoint) {}

    /// Closes the socket, releasing any associated resources.
    ~socket() {
        if (m_handle != -1) {
            close(m_handle);
        }
    }

    socket& operator=(socket&& other) noexcept {
        const auto handle = std::exchange(other.m_handle, -1);
        if (m_handle != -1) {
            close(m_handle);
        }

        m_handle = handle;
        m_recv_flags = other.m_recv_flags;
        m_local_endpoint = other.m_local_endpoint;
        m_remote_endpoint = other.m_remote_endpoint;

        return *this;
    }

    /// Get the socket handle associated with this socket.
    auto native_handle() const noexcept { return m_handle; }

    /// Get the address and port of the local end-point.
    const ip_endpoint& local_endpoint() const noexcept { return m_local_endpoint; }

    /// Get the address and port of the remote end-point.
    const ip_endpoint& remote_endpoint() const noexcept { return m_remote_endpoint; }

    /// Bind the local end of this socket to the specified local end-point.
    void bind(const ip_endpoint& local_endpoint) {
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

        int result = ::bind(m_handle, sockaddr, sockaddr_len);
        if (result != 0) {
            throw std::system_error(last_error, std::system_category(), "Error binding to endpoint: bind()");
        }

        SOCKADDR_STORAGE bound_sockaddr_storage = {};
        auto* bound_sockaddr = reinterpret_cast<SOCKADDR*>(&bound_sockaddr_storage);
        socklen_t bound_sockaddr_len = sizeof(bound_sockaddr_storage);
        result = getsockname(m_handle, bound_sockaddr, &bound_sockaddr_len);
        if (result == 0) {
            m_local_endpoint = detail::sockaddr_to_ip_endpoint(*bound_sockaddr);
        }
        else {
            m_local_endpoint = local_endpoint;
        }
    }

	/// Put the socket into a passive listening state that will start acknowledging
	/// and queueing up new connections ready to be accepted by a call to 'accept()'.
	///
	/// The backlog of connections ready to be accepted will be set to some default
	/// suitable large value, depending on the network provider. If you need more
	/// control over the size of the queue then use the overload of listen()
	/// that accepts a 'backlog' parameter.
	///
	/// \throws std::system_error
	/// If the socket could not be placed into a listening mode.
    void listen() const {
        if (const int result = ::listen(m_handle, SOMAXCONN); result != 0) {
            throw std::system_error(
                last_error,
                std::system_category(),
                "Failed to start listening on bound endpoint: listen");
        }
    }

	/// Put the socket into a passive listening state that will start acknowledging
	/// and queueing up new connections ready to be accepted by a call to 'accept()'.
	///
	/// \param backlog
	/// The maximum number of pending connections to allow in the queue of ready-to-accept
	/// connections.
	///
	/// \throws std::system_error
	/// If the socket could not be placed into a listening mode.
    void listen(std::uint32_t backlog) const {
        if (backlog > 0x7FFFFFFF) {
            backlog = 0x7FFFFFFF;
        }

        if (const int result = ::listen(m_handle, static_cast<int>(backlog)); result != 0) {
            throw std::system_error(
                last_error,
                std::system_category(),
                "Failed to start listening on bound endpoint: listen");
        }
    }

	/// Connect the socket to the specified remote end-point.
	///
	/// The socket must be in a bound but unconnected state prior to this call.
	///
	/// \param remote_endpoint
	/// The IP address and port-number to connect to.
	///
	/// \return
	/// An awaitable object that must be co_await'ed to perform the async connect
	/// operation. The result of the co_await expression is type void.
    [[nodiscard]] socket_connect_operation connect(const ip_endpoint& remote_endpoint) noexcept;

	/// Connect to the specified remote end-point.
	///
	/// \param remote_endpoint
	/// The IP address and port of the remote end-point to connect to.
	///
	/// \param ct
	/// A cancellation token that can be used to communicate a request to
	/// later cancel the operation. If the operation is successfully
	/// cancelled then it will complete by throwing a cppcoro::operation_cancelled
	/// exception.
	///
	/// \return
	/// An awaitable object that will start the connect operation when co_await'ed
	/// and will suspend the coroutine, resuming it when the operation completes.
	/// The result of the co_await expression has type 'void'.
    [[nodiscard]] socket_connect_operation_cancellable connect(const ip_endpoint& remote_endpoint, cancellation_token ct) noexcept;

    [[nodiscard]] socket_accept_operation accept(socket& accepting_socket) noexcept;
    [[nodiscard]] socket_accept_operation_cancellable accept(socket& accepting_socket, cancellation_token ct) noexcept;

    [[nodiscard]] socket_disconnect_operation disconnect() noexcept;
    [[nodiscard]] socket_disconnect_operation_cancellable disconnect(cancellation_token ct) noexcept;

    [[nodiscard]] socket_send_operation send(const void* buffer, std::size_t size) noexcept;
    [[nodiscard]] socket_send_operation_cancellable send(const void* buffer, std::size_t size, cancellation_token ct) noexcept;

    [[nodiscard]] socket_recv_operation recv(void* buffer, std::size_t size) noexcept;
    [[nodiscard]] socket_recv_operation_cancellable recv(void* buffer, std::size_t size, cancellation_token ct) noexcept;

    [[nodiscard]] socket_recv_from_operation recv_from(void* buffer, std::size_t size) noexcept;
    [[nodiscard]] socket_recv_from_operation_cancellable recv_from(void* buffer, std::size_t size, cancellation_token ct) noexcept;

    [[nodiscard]] socket_send_to_operation send_to(const ip_endpoint& destination, const void* buffer, std::size_t size) noexcept;
    [[nodiscard]] socket_send_to_operation_cancellable send_to(const ip_endpoint& destination, const void* buffer, std::size_t size, cancellation_token ct) noexcept;

    void close_send() const {
        if (const int result = shutdown(m_handle, SHUT_WR); result == -1) {
            throw std::system_error(
                last_error,
                std::system_category(),
                "Failed to close socket send stream: shutdown(SHUT_WR)");
        }
    }
    void close_recv() const {
        if (const int result = shutdown(m_handle, SHUT_RD); result == -1) {
            throw std::system_error(
                last_error,
                std::system_category(),
                "Failed to close socket receive stream: shutdown(SHUT_RD)");
        }
    }

private:
    static int create_socket(int domain, int type, int protocol) {
        // 创建套接字
        const int fd = ::socket(domain, type, protocol);
        if (fd == -1) {
            throw std::system_error(errno, std::system_category(), "创建套接字时出错");
        }
        return fd;
    }

    explicit socket(coro::detail::macos::io_queue& io_queue, coro::detail::macos::fd_t fd) noexcept
        : m_handle(fd)
        , m_io_queue(io_queue) {}

	friend class socket_accept_operation_impl;
	friend class socket_connect_operation_impl;
    friend class socket_recv_from_operation_impl;
    friend class socket_recv_operation_impl;
    friend class socket_send_to_operation_impl;

    coro::detail::macos::fd_t m_handle = -1;
    coro::detail::macos::io_queue& m_io_queue;
    int m_recv_flags = 0;
    ip_endpoint m_local_endpoint;
    ip_endpoint m_remote_endpoint;
};

}

}

#endif //SOCKET_H
