//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/net/socket_recv_from_operation.h"
#include "../../include/coro/net/socket.h"

bool coro::net::socket_recv_from_operation_impl::try_start(coro::detail::io_operation_base& operation) noexcept {
    // 初始化用于接收数据的 iovec 和 msghdr 结构
    iovec vec{};
    vec.iov_base = m_buffer;
    vec.iov_len = m_byte_count;

    msghdr msgHdr{};
    std::memset(&msgHdr, 0, sizeof(msgHdr));
    msgHdr.msg_name = &m_source_storage;
    msgHdr.msg_namelen = m_source_addr_len;
    msgHdr.msg_iov = &vec;
    msgHdr.msg_iovlen = 1;

    // 使用 recvmsg 注册事件
    return operation.m_io_queue.transaction(operation.m_message)
        .recvmsg(m_socket.native_handle(), &msgHdr, 0)
        .commit();
}

void coro::net::socket_recv_from_operation_impl::cancel(coro::detail::io_operation_base &operation) noexcept {
    operation.m_io_queue.transaction(operation.m_message).cancel().commit();
}

std::tuple<std::size_t, coro::net::ip_endpoint> coro::net::socket_recv_from_operation_impl::get_result(coro::detail::io_operation_base &operation) {
    auto size = operation.get_result(); // 可能抛出错误
    if (size > m_byte_count) {
        throw std::system_error{
            EMSGSIZE, std::generic_category(), "Received data exceeds buffer size"
        };
    }

    ip_endpoint senderEndpoint = detail::sockaddr_to_ip_endpoint(
        *reinterpret_cast<sockaddr*>(&m_source_storage));

    return std::make_tuple(size, senderEndpoint);
}
