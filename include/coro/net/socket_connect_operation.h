//
// Created by Jesson on 2024/10/11.
//

#ifndef SOCKET_CONNECT_OPERATION_H
#define SOCKET_CONNECT_OPERATION_H

#include "../cancellation/cancellation_token.h"
#include "../detail/macos_io_operation.h"
#include "ip_endpoint.h"

namespace coro::net {

class socket;

class socket_connect_operation_impl {
public:
	socket_connect_operation_impl(socket& socket, const ip_endpoint& remote_endpoint) noexcept
		: m_socket(socket)
		, m_remote_endpoint(remote_endpoint) {}

	bool try_start(detail::io_operation_base& operation) const noexcept;
	void cancel(detail::io_operation_base& operation) noexcept;
	void get_result(detail::io_operation_base& operation) const;

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

#endif //SOCKET_CONNECT_OPERATION_H
