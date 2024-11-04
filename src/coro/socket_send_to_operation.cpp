//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/net/socket_send_to_operation.h"
#include "../../include/coro/net/socket.h"

bool coro::net::socket_send_to_operation_impl::try_start(coro::detail::io_operation_base& operation) noexcept {
    return operation.m_io_queue.transaction(&operation.m_message)
        .sendmsg(m_socket.native_handle(), &m_msg_hdr, 0)  // 注册 sendmsg 操作
        .commit();
}

void coro::net::socket_send_to_operation_impl::cancel(coro::detail::io_operation_base& operation) noexcept {
    operation.m_io_queue.transaction(&operation.m_message).cancel().commit();
}

