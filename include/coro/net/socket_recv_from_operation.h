//
// Created by Jesson on 2024/10/11.
//

#ifndef SOCKET_RECV_FROM_OPERATION_H
#define SOCKET_RECV_FROM_OPERATION_H

#include "ip_endpoint.h"
#include "../cancellation/cancellation_token.h"
#include "../detail/macos_io_operation.h"

#include <cstring>
#include <tuple>

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

        // Initialize iovec
        std::memset(&m_iov, 0, sizeof(m_iov));
        m_iov.iov_base = m_buffer;
        m_iov.iov_len = m_byte_count;

        // Initialize msghdr
        std::memset(&m_msg_hdr, 0, sizeof(m_msg_hdr));
        m_msg_hdr.msg_name = &m_source_storage;
        m_msg_hdr.msg_namelen = m_source_addr_len;
        m_msg_hdr.msg_iov = &m_iov;
        m_msg_hdr.msg_iovlen = 1;
        m_msg_hdr.msg_control = nullptr;
        m_msg_hdr.msg_controllen = 0;
        m_msg_hdr.msg_flags = 0;
    }

    bool try_start(coro::detail::io_operation_base& operation) noexcept;
    void cancel(coro::detail::io_operation_base& operation) noexcept;
    std::tuple<std::size_t, ip_endpoint> get_result(coro::detail::io_operation_base& operation);

private:
    socket& m_socket;
    void* m_buffer;
    std::size_t m_byte_count;
    sockaddr_storage m_source_storage;
    socklen_t m_source_addr_len;
    msghdr m_msg_hdr;
    iovec m_iov;
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

#endif //SOCKET_RECV_FROM_OPERATION_H
