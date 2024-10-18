//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/net/socket_accept_operation.h"
#include "../../include/coro/net/socket.h"

bool coro::net::socket_accept_operation_impl::try_start(
    coro::detail::io_operation_base &operation) noexcept {
    return operation.m_io_queue.transaction(operation.m_message)
        .accept(m_listen_sock.native_handle(), &m_address_buf[0], &m_buf_len)
        .commit();
}

void coro::net::socket_accept_operation_impl::cancel(coro::detail::io_operation_base &operation) noexcept {
    operation.m_io_queue.transaction(operation.m_message).cancel().commit();
}

void coro::net::socket_accept_operation_impl::get_result(coro::detail::io_operation_base &operation) {
    const auto fd = operation.get_result();
    m_accept_sock = socket(operation.m_io_queue, fd);
    m_buf_len = sizeof(m_address_buf);
    if (getpeername(fd, reinterpret_cast<sockaddr *>(&m_address_buf[0]), &m_buf_len) < 0) {
        throw std::system_error{
            errno,
            std::generic_category()
        };
    }
    m_accept_sock.m_remote_endpoint = detail::sockaddr_to_ip_endpoint(
        std::ref(*reinterpret_cast<sockaddr *>(&m_address_buf[0])));
    m_buf_len = sizeof(m_address_buf);
    if (getsockname(fd, reinterpret_cast<sockaddr *>(&m_address_buf[0]), &m_buf_len) < 0) {
        throw std::system_error{
            errno,
            std::generic_category()
        };
    }
    m_accept_sock.m_local_endpoint = detail::sockaddr_to_ip_endpoint(
        std::ref(*reinterpret_cast<sockaddr *>(&m_address_buf[0])));
}

