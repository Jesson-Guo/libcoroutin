//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/net/socket_recv_from_operation.h"
#include "../../include/coro/net/socket.h"

bool coro::net::socket_recv_from_operation_impl::try_start(coro::detail::io_operation_base& operation) noexcept {
    return operation.m_io_queue.transaction(&operation.m_message)
        .recvmsg(m_socket.native_handle(), &m_msg_hdr, 0)
        .commit();
}

void coro::net::socket_recv_from_operation_impl::cancel(coro::detail::io_operation_base &operation) noexcept {
    operation.m_io_queue.transaction(&operation.m_message).cancel().commit();
}

std::tuple<std::size_t, coro::net::ip_endpoint> coro::net::socket_recv_from_operation_impl::get_result(coro::detail::io_operation_base &operation) {
    auto size = operation.get_result(); // 可能抛出错误

    // Check for message truncation
    if (m_msg_hdr.msg_flags & MSG_TRUNC) {
        throw std::system_error{
            EMSGSIZE, std::generic_category(), "Received data exceeds buffer size"
        };
    }
    // if (size > m_byte_count) {
    //     throw std::system_error{
    //         EMSGSIZE, std::generic_category(), "Received data exceeds buffer size"
    //     };
    // }
    ip_endpoint source_endpoint = detail::sockaddr_to_ip_endpoint(*reinterpret_cast<sockaddr*>(&m_source_storage));
    return std::make_tuple(size, source_endpoint);
}
