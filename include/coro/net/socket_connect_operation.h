//
// Created by Jesson on 2024/10/11.
//

#ifndef SOCKET_CONNECT_OPERATION_H
#define SOCKET_CONNECT_OPERATION_H

#include "../cancellation/cancellation_token.h"
#include "../cancellation/cancellation_registration.h"
#include "../detail/macos_io_operation.h"
#include "ip_endpoint.h"
#include "socket.h"
#include "socket_helpers.h"

#include <cassert>

namespace coro::net {

class socket;

class socket_connect_operation_impl {
public:
	socket_connect_operation_impl(socket& socket, const ip_endpoint& remote_endpoint) noexcept
		: m_socket(socket)
		, m_remote_endpoint(remote_endpoint) {}

	bool try_start(coro::detail::io_operation_base& operation) noexcept;
	void cancel(coro::detail::io_operation_base& operation) noexcept;
	void get_result(coro::detail::io_operation_base& operation);

private:
	socket& m_socket;
	ip_endpoint m_remote_endpoint;
};

class socket_connect_operation : public coro::detail::io_operation<socket_connect_operation> {
public:
	socket_connect_operation(coro::detail::macos::io_queue &io_queue, socket& socket, const ip_endpoint& remote_endpoint) noexcept
        : io_operation {io_queue}
		, m_connect_op_impl(socket, remote_endpoint) {}

private:
	friend io_operation;

	bool try_start() noexcept { return m_connect_op_impl.try_start(*this); }
	decltype(auto) get_result() { return m_connect_op_impl.get_result(*this); }

	socket_connect_operation_impl m_connect_op_impl;
};

class socket_connect_operation_cancellable : public coro::detail::io_operation_cancellable<socket_connect_operation_cancellable> {
public:
	socket_connect_operation_cancellable(
	    coro::detail::macos::io_queue &io_queue, socket& socket, const ip_endpoint& remote_endpoint, cancellation_token&& ct) noexcept
		: io_operation_cancellable {io_queue, std::move(ct)}
		, m_connect_op_impl(socket, remote_endpoint) {}

private:
	friend io_operation_cancellable;

	bool try_start() noexcept { return m_connect_op_impl.try_start(*this); }
	void cancel() noexcept { m_connect_op_impl.cancel(*this); }
	void get_result() { m_connect_op_impl.get_result(*this); }

	socket_connect_operation_impl m_connect_op_impl;
};

}

bool coro::net::socket_connect_operation_impl::try_start(coro::detail::io_operation_base& operation) noexcept {
    SOCKADDR_STORAGE remote_sockaddr_storage{};
    const int remoteLength = detail::ip_endpoint_to_sockaddr(m_remote_endpoint, std::ref(remote_sockaddr_storage));
    return operation.m_io_queue.transaction(operation.m_message)
        .connect(m_socket.native_handle(), &remote_sockaddr_storage, remoteLength)
        .commit();
}

void coro::net::socket_connect_operation_impl::cancel(coro::detail::io_operation_base& operation) noexcept {
    operation.m_io_queue.transaction(operation.m_message).cancel().commit();
}

void coro::net::socket_connect_operation_impl::get_result(coro::detail::io_operation_base& operation) {
    SOCKADDR_STORAGE remote_sockaddr_storage{};
    socklen_t remote_sock_len = sizeof(remote_sockaddr_storage);
    if(getpeername(m_socket.native_handle(), reinterpret_cast<sockaddr*>(&remote_sockaddr_storage), &remote_sock_len) < 0) {
        throw std::system_error{
            errno,
            std::generic_category()
        };
    }
    m_socket.m_remote_endpoint = detail::sockaddr_to_ip_endpoint(std::ref(*reinterpret_cast<sockaddr*>(&remote_sockaddr_storage)));
}

#endif //SOCKET_CONNECT_OPERATION_H
