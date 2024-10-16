//
// Created by Jesson on 2024/10/11.
//

#ifndef SOCKET_RECV_FROM_OPERATION_H
#define SOCKET_RECV_FROM_OPERATION_H

#include "ip_endpoint.h"
#include "socket.h"
#include <cstring>
#include <sys/socket.h>
#include <tuple>
#include <sys/stat.h>

namespace coro::net {

class socket;
class socket_recv_from_operation_impl {
public:
    socket_recv_from_operation_impl(socket& socket, void* buffer, std::size_t byte_count) noexcept
        : m_socket(socket)
        , m_buffer(buffer)
        , m_byte_count(byte_count) {
        // 初始化存储发送方地址的结构体
        std::memset(&m_source_storage, 0, sizeof(m_source_storage));
        m_source_addr_len = sizeof(m_source_storage);
    }

    bool try_start(coro::detail::io_operation_base& operation) noexcept;
    void cancel(coro::detail::io_operation_base& operation) noexcept;
    std::tuple<std::size_t, ip_endpoint>
    get_result(coro::detail::io_operation_base& operation);

private:
    socket& m_socket;
    void* m_buffer;
    std::size_t m_byte_count;
    sockaddr_storage m_source_storage;
    socklen_t m_source_addr_len;
};

class socket_recv_from_operation : public coro::detail::io_operation<socket_recv_from_operation> {
public:
    socket_recv_from_operation(coro::detail::macos::io_queue& io_queue, socket& socket, void* buffer, std::size_t byte_count) noexcept
        : io_operation{io_queue}
        , m_recv_from_impl(socket, buffer, byte_count) {}

private:
    friend io_operation;

    bool try_start() noexcept { return m_recv_from_impl.try_start(*this); }
    decltype(auto) get_result() { return m_recv_from_impl.get_result(*this); }

    socket_recv_from_operation_impl m_recv_from_impl;
};

class socket_recv_from_operation_cancellable : public coro::detail::io_operation_cancellable<socket_recv_from_operation_cancellable> {
public:
    socket_recv_from_operation_cancellable(
        coro::detail::macos::io_queue& io_queue, socket& socket, void* buffer, std::size_t byte_count, cancellation_token&& ct) noexcept
        : io_operation_cancellable{io_queue, std::move(ct)}
        , m_recv_from_impl(socket, buffer, byte_count) {}

private:
    friend io_operation_cancellable;

    bool try_start() noexcept { return m_recv_from_impl.try_start(*this); }
    void cancel() noexcept { m_recv_from_impl.cancel(*this); }
    decltype(auto) get_result() { return m_recv_from_impl.get_result(*this); }

    socket_recv_from_operation_impl m_recv_from_impl;
};

}

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

#endif //SOCKET_RECV_FROM_OPERATION_H
