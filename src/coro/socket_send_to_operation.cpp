//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/net/socket_send_to_operation.h"

bool coro::net::socket_send_to_operation_impl::try_start(coro::detail::io_operation_base& operation) noexcept {
    // 准备 iovec 结构，指向要发送的数据缓冲区
    iovec vec{};
    vec.iov_base = const_cast<void*>(m_buffer);  // 数据缓冲区
    vec.iov_len = m_byte_count;                  // 数据大小

    // 准备目标地址的 sockaddr_storage
    sockaddr_storage dest_addr_storage{};
    socklen_t dest_addr_len = 0;

    // 处理 IPv4 和 IPv6 地址
    if (m_destination.is_ipv4()) {
        auto& dest_addr = reinterpret_cast<SOCKADDR_IN&>(dest_addr_storage);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(m_destination.to_ipv4().port());
        std::memcpy(&dest_addr.sin_addr, m_destination.to_ipv4().address().bytes(), 4);
        dest_addr_len = sizeof(SOCKADDR_IN);
    }
    else {
        auto& dest_addr = reinterpret_cast<SOCKADDR_IN6&>(dest_addr_storage);
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(m_destination.to_ipv6().port());
        std::memcpy(&dest_addr.sin6_addr, m_destination.to_ipv6().address().bytes(), 16);
        dest_addr_len = sizeof(SOCKADDR_IN6);
    }

    // 准备 msghdr 结构，描述发送的消息
    msghdr msg_hdr{};
    std::memset(&msg_hdr, 0, sizeof(msg_hdr));
    msg_hdr.msg_name = &dest_addr_storage;         // 目标地址
    msg_hdr.msg_namelen = dest_addr_len;           // 地址长度
    msg_hdr.msg_iov = &vec;                      // 数据缓冲区
    msg_hdr.msg_iovlen = 1;                      // 缓冲区数量

    // 使用 io_transaction 进行异步发送
    return operation.m_io_queue.transaction(operation.m_message)
        .sendmsg(m_socket.native_handle(), &msg_hdr, 0)  // 注册 sendmsg 操作
        .commit();
}

void coro::net::socket_recv_from_operation_impl::cancel(coro::detail::io_operation_base& operation) noexcept {
    operation.m_io_queue.transaction(operation.m_message).cancel().commit();
}

