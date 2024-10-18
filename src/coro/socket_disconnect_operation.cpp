//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/net/socket_disconnect_operation.h"
#include "../../include/coro/net/socket.h"

bool coro::net::socket_disconnect_operation_impl::try_start(coro::detail::io_operation_base& operation) const noexcept {
    return operation.m_io_queue.transaction(operation.m_message).close(m_socket.native_handle()).commit();
}

void coro::net::socket_disconnect_operation_impl::cancel(coro::detail::io_operation_base& operation) noexcept {
    operation.m_io_queue.transaction(operation.m_message).cancel().commit();
}

void coro::net::socket_disconnect_operation_impl::get_result(coro::detail::io_operation_base& operation) {
    operation.get_result();
}

