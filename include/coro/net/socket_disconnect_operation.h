//
// Created by Jesson on 2024/10/11.
//

#ifndef SOCKET_DISCONNECT_OPERATION_H
#define SOCKET_DISCONNECT_OPERATION_H

#include "../cancellation/cancellation_token.h"
#include "../detail/macos_io_operation.h"

namespace coro::net {

class socket;

class socket_disconnect_operation_impl {
public:
	explicit socket_disconnect_operation_impl(socket& socket) noexcept : m_socket(socket) {}

	bool try_start(coro::detail::io_operation_base& operation) const noexcept;
	void cancel(coro::detail::io_operation_base& operation) noexcept;
	void get_result(coro::detail::io_operation_base& operation);

private:
	socket& m_socket;
};

class socket_disconnect_operation : public coro::detail::io_operation<socket_disconnect_operation> {
public:
	socket_disconnect_operation(coro::detail::macos::io_queue& io_queue, socket& socket) noexcept
		: io_operation {io_queue}
		, m_disconnect_op_impl(socket) {}

private:
	friend io_operation;

	bool try_start() noexcept { return m_disconnect_op_impl.try_start(*this); }
	void get_result() { m_disconnect_op_impl.get_result(*this); }

	socket_disconnect_operation_impl m_disconnect_op_impl;
};

class socket_disconnect_operation_cancellable : public coro::detail::io_operation_cancellable<socket_disconnect_operation_cancellable> {
public:
	socket_disconnect_operation_cancellable(coro::detail::macos::io_queue& io_queue, socket& socket, cancellation_token&& ct) noexcept
		: io_operation_cancellable {io_queue, std::move(ct)}
		, m_disconnect_op_impl(socket) {}

private:
	friend io_operation_cancellable;

	bool try_start() noexcept { return m_disconnect_op_impl.try_start(*this); }
	void cancel() noexcept { m_disconnect_op_impl.cancel(*this); }
	void get_result() { m_disconnect_op_impl.get_result(*this); }

	socket_disconnect_operation_impl m_disconnect_op_impl;
};

}

#endif //SOCKET_DISCONNECT_OPERATION_H
