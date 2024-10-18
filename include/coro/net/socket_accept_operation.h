//
// Created by Jesson on 2024/10/11.
//

#ifndef SOCKET_ACCEPT_OPERATION_H
#define SOCKET_ACCEPT_OPERATION_H

#include "../cancellation/cancellation_token.h"
#include "../detail/macos_io_operation.h"

namespace coro::net {

class socket;

class socket_accept_operation_impl {
public:
	socket_accept_operation_impl(socket& listen_sock, socket& accept_sock) noexcept
		: m_listen_sock(listen_sock)
		, m_accept_sock(accept_sock) {}

	bool try_start(coro::detail::io_operation_base& operation) noexcept;
	void cancel(coro::detail::io_operation_base& operation) noexcept;
	void get_result(coro::detail::io_operation_base& operation);

private:
	socket& m_listen_sock;
	socket& m_accept_sock;
	alignas(8) std::uint8_t m_address_buf[88];
	socklen_t m_buf_len = sizeof(m_address_buf);
};

class socket_accept_operation : public coro::detail::io_operation<socket_accept_operation> {
public:
	socket_accept_operation(coro::detail::macos::io_queue& io_queue, socket& listen_sock, socket& accept_sock) noexcept
		: io_operation{io_queue}
		, m_accept_op_impl(listen_sock, accept_sock) {}

private:
	friend io_operation;

	bool try_start() noexcept { return m_accept_op_impl.try_start(*this); }
	void get_result() { m_accept_op_impl.get_result(*this); }

	socket_accept_operation_impl m_accept_op_impl;
};

class socket_accept_operation_cancellable : public coro::detail::io_operation_cancellable<socket_accept_operation_cancellable> {
public:
	socket_accept_operation_cancellable(
	    coro::detail::macos::io_queue& io_queue, socket& listen_sock, socket& accept_sock, cancellation_token&& ct) noexcept
		: io_operation_cancellable(io_queue, std::move(ct))
		, m_accept_op_impl(listen_sock, accept_sock) {}

private:
	friend io_operation_cancellable;

	bool try_start() noexcept { return m_accept_op_impl.try_start(*this); }
	void cancel() noexcept { m_accept_op_impl.cancel(*this); }
	void get_result() { m_accept_op_impl.get_result(*this); }

	socket_accept_operation_impl m_accept_op_impl;
};

}

#endif //SOCKET_ACCEPT_OPERATION_H
