//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/net/socket_recv_operation.h"
#include "../../include/coro/net/socket.h"

bool coro::net::socket_recv_operation_impl::try_start(coro::detail::io_operation_base& operation) const noexcept {
    return operation.m_io_queue.transaction(operation.m_message)
        .read(m_socket.native_handle(), m_buffer, m_byte_count)
        .commit();
}

void coro::net::socket_recv_operation_impl::cancel(coro::detail::io_operation_base& operation) noexcept {
    // TODO 取消事件的方法可能需要特殊处理
    operation.m_io_queue.transaction(operation.m_message).cancel().commit();
}

std::size_t coro::net::socket_recv_operation_impl::get_result(coro::detail::io_operation_base& operation) const {
    auto size = operation.get_result();
    // 检查是否接收到的数据超出缓冲区大小
    if (size > m_byte_count) {
        throw std::system_error{
            EAGAIN, std::generic_category(), "Received data exceeds buffer size"
        };
    }
    return size;
}

