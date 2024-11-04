//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/net/socket_send_operation.h"
#include "../../include/coro/net/socket.h"

bool coro::net::socket_send_operation_impl::try_start(coro::detail::io_operation_base& operation) const noexcept {
    return operation.m_io_queue.transaction(&operation.m_message)
        .send(m_socket.native_handle(), m_buffer, m_byte_count)
        .commit();
}

void coro::net::socket_send_operation_impl::cancel(coro::detail::io_operation_base& operation) noexcept {
    operation.m_io_queue.transaction(&operation.m_message).cancel().commit();
}

