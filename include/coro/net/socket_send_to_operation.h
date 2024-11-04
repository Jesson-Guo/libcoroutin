//
// Created by Jesson on 2024/10/12.
//

#ifndef SOCKET_SEND_TO_OPERATION_H
#define SOCKET_SEND_TO_OPERATION_H

#include "ip_endpoint.h"
#include "../cancellation/cancellation_token.h"
#include "../detail/macos_io_operation.h"

#include <netinet/in.h>

namespace coro::net {

class socket;
class socket_send_to_operation_impl {
public:
    socket_send_to_operation_impl(socket& s, const ip_endpoint& destination, const void* buffer, std::size_t byte_count) noexcept
        : m_socket(s)
        , m_destination(destination)
        , m_buffer(buffer)
        , m_byte_count(byte_count) {
        std::memset(&m_dest_storage, 0, sizeof(m_dest_storage));
        if (m_destination.is_ipv4()) {
            auto& dest_addr = reinterpret_cast<sockaddr_in&>(m_dest_storage);
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(m_destination.to_ipv4().port());
            std::memcpy(&dest_addr.sin_addr, m_destination.to_ipv4().address().bytes(), 4);
            m_dest_addr_len = sizeof(sockaddr_in);
        }
        else {
            auto& dest_addr = reinterpret_cast<sockaddr_in6&>(m_dest_storage);
            dest_addr.sin6_family = AF_INET6;
            dest_addr.sin6_port = htons(m_destination.to_ipv6().port());
            std::memcpy(&dest_addr.sin6_addr, m_destination.to_ipv6().address().bytes(), 16);
            m_dest_addr_len = sizeof(sockaddr_in6);
        }
        m_iov.iov_base = const_cast<void*>(m_buffer); // sendmsg requires non-const iov_base
        m_iov.iov_len = m_byte_count;

        std::memset(&m_msg_hdr, 0, sizeof(m_msg_hdr));
        m_msg_hdr.msg_name = &m_dest_storage;
        m_msg_hdr.msg_namelen = m_dest_addr_len;
        m_msg_hdr.msg_iov = &m_iov;
        m_msg_hdr.msg_iovlen = 1;
        m_msg_hdr.msg_control = nullptr;
        m_msg_hdr.msg_controllen = 0;
        m_msg_hdr.msg_flags = 0;
    }

    bool try_start(coro::detail::io_operation_base& operation) noexcept;
    void cancel(coro::detail::io_operation_base& operation) noexcept;

private:
    socket& m_socket;
    ip_endpoint m_destination;
    const void* m_buffer;
    std::size_t m_byte_count;
    sockaddr_storage m_dest_storage;
    socklen_t m_dest_addr_len;
    msghdr m_msg_hdr;
    iovec m_iov;
};

class socket_send_to_operation : public coro::detail::io_operation<socket_send_to_operation> {
public:
    socket_send_to_operation(
        coro::detail::macos::io_queue& io_queue, socket& s, const ip_endpoint& destination, const void* buffer, std::size_t byte_count) noexcept
        : io_operation {io_queue}
        , m_send_to_impl(s, destination, buffer, byte_count) {}

private:
    friend io_operation;

    bool try_start() noexcept { return m_send_to_impl.try_start(*this); }

    socket_send_to_operation_impl m_send_to_impl;
};

class socket_send_to_operation_cancellable : public coro::detail::io_operation_cancellable<socket_send_to_operation_cancellable> {
public:
    socket_send_to_operation_cancellable(
        coro::detail::macos::io_queue& io_queue, socket& s, const ip_endpoint& destination, const void* buffer, std::size_t byte_count, cancellation_token&& ct) noexcept
        : io_operation_cancellable {io_queue, std::move(ct)}
        , m_send_to_impl(s, destination, buffer, byte_count) {}

private:
    friend io_operation_cancellable;

    bool try_start() noexcept { return m_send_to_impl.try_start(*this); }
    void cancel() noexcept { m_send_to_impl.cancel(*this); }

    socket_send_to_operation_impl m_send_to_impl;
};

}

#endif //SOCKET_SEND_TO_OPERATION_H
