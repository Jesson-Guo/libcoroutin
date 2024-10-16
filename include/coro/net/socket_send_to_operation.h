//
// Created by Jesson on 2024/10/12.
//

#ifndef SOCKET_SEND_TO_OPERATION_H
#define SOCKET_SEND_TO_OPERATION_H

#include "socket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <system_error>

namespace coro::net {

class socket;
class socket_send_to_operation_impl {
public:
    socket_send_to_operation_impl(socket& s, const ip_endpoint& destination, const void* buffer, std::size_t byte_count) noexcept
        : m_socket(s)
        , m_destination(destination)
        , m_buffer(buffer)
        , m_byte_count(byte_count) {}

    bool try_start(coro::detail::io_operation_base& operation) noexcept;
    void cancel(coro::detail::io_operation_base& operation) noexcept;

private:
    socket& m_socket;
    ip_endpoint m_destination;
    const void* m_buffer;
    std::size_t m_byte_count;
};

class socket_send_to_operation : public coro::detail::io_operation<socket_send_to_operation> {
public:
    socket_send_to_operation(
        coro::detail::macos::io_queue& io_queue, socket& s, const ip_endpoint& destination, const void* buffer, std::size_t byte_count) noexcept
        : io_operation {io_queue}
        , m_impl(s, destination, buffer, byte_count) {}

private:
    friend io_operation;

    bool try_start() noexcept { return m_impl.try_start(*this); }

    socket_send_to_operation_impl m_impl;
};

class socket_send_to_operation_cancellable : public coro::detail::io_operation_cancellable<socket_send_to_operation_cancellable> {
public:
    socket_send_to_operation_cancellable(
        coro::detail::macos::io_queue& io_queue, socket& s, const ip_endpoint& destination, const void* buffer, std::size_t byte_count, cancellation_token&& ct) noexcept
        : io_operation_cancellable {io_queue, std::move(ct)}
        , m_impl(s, destination, buffer, byte_count) {}

private:
    friend io_operation_cancellable;

    bool try_start() noexcept { return m_impl.try_start(*this); }
    void cancel() noexcept { m_impl.cancel(*this); }

    socket_send_to_operation_impl m_impl;
};

}

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
    msghdr msgHdr{};
    std::memset(&msgHdr, 0, sizeof(msgHdr));
    msgHdr.msg_name = &dest_addr_storage;         // 目标地址
    msgHdr.msg_namelen = dest_addr_len;           // 地址长度
    msgHdr.msg_iov = &vec;                      // 数据缓冲区
    msgHdr.msg_iovlen = 1;                      // 缓冲区数量

    // 使用 io_transaction 进行异步发送
    return operation.m_io_queue.transaction(operation.m_message)
        .sendmsg(m_socket.native_handle(), &msgHdr, 0)  // 注册 sendmsg 操作
        .commit();
}

void coro::net::socket_recv_from_operation_impl::cancel(coro::detail::io_operation_base& operation) noexcept {
    operation.m_io_queue.transaction(operation.m_message).cancel().commit();
}

#endif //SOCKET_SEND_TO_OPERATION_H
