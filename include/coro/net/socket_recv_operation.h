//
// Created by Jesson on 2024/10/12.
//

#ifndef SOCKET_RECV_OPERATION_H
#define SOCKET_RECV_OPERATION_H

#include "../cancellation/cancellation_token.h"
#include "../detail/macos_io_operation.h"

namespace coro::net {

class socket;
class socket_recv_operation_impl {
public:
	socket_recv_operation_impl(socket& s, void* buffer, std::size_t size) noexcept
		: m_socket(s)
		, m_buffer(buffer)
        , m_byte_count(size) {}

	bool try_start(coro::detail::io_operation_base& operation) const noexcept;
	void cancel(coro::detail::io_operation_base& operation) noexcept;
	std::size_t get_result(coro::detail::io_operation_base& operation) const;

private:
    socket& m_socket;
    void* m_buffer;
    std::size_t m_byte_count;
};

class socket_recv_operation : public coro::detail::io_operation<socket_recv_operation> {
public:
	socket_recv_operation(coro::detail::macos::io_queue& io_queue, socket& s, void* buffer, std::size_t size) noexcept
		: io_operation {io_queue}
		, m_recv_op_impl(s, buffer, size) {}

private:
	friend io_operation;

	bool try_start() noexcept { return m_recv_op_impl.try_start(*this); }
	std::size_t get_result() { return m_recv_op_impl.get_result(*this); }

	socket_recv_operation_impl m_recv_op_impl;
};

class socket_recv_operation_cancellable : public coro::detail::io_operation_cancellable<socket_recv_operation_cancellable> {
public:
	socket_recv_operation_cancellable(
	    coro::detail::macos::io_queue& io_queue, socket& s, void* buffer, std::size_t size, cancellation_token&& ct) noexcept
		: io_operation_cancellable {io_queue, std::move(ct)}
		, m_recv_op_impl(s, buffer, size) {}

private:
	friend io_operation_cancellable;

    bool try_start() noexcept { return m_recv_op_impl.try_start(*this); }
    void cancel() noexcept { m_recv_op_impl.cancel(*this); }
    std::size_t get_result() { return m_recv_op_impl.get_result(*this); }

	socket_recv_operation_impl m_recv_op_impl;
};

}

#endif //SOCKET_RECV_OPERATION_H
