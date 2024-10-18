//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/net/socket_connect_operation.h"
#include "../../include/coro/net/socket.h"

bool coro::net::socket_connect_operation_impl::try_start(coro::detail::io_operation_base& operation) const noexcept {
    SOCKADDR_STORAGE remote_sockaddr_storage{};
    const int remoteLength = detail::ip_endpoint_to_sockaddr(m_remote_endpoint, std::ref(remote_sockaddr_storage));
    return operation.m_io_queue.transaction(operation.m_message)
        .connect(m_socket.native_handle(), &remote_sockaddr_storage, remoteLength)
        .commit();
}

void coro::net::socket_connect_operation_impl::cancel(coro::detail::io_operation_base& operation) noexcept {
    operation.m_io_queue.transaction(operation.m_message).cancel().commit();
}

void coro::net::socket_connect_operation_impl::get_result(coro::detail::io_operation_base& operation) const {
    SOCKADDR_STORAGE remote_sockaddr_storage{};
    socklen_t remote_sock_len = sizeof(remote_sockaddr_storage);
    if(getpeername(m_socket.native_handle(), reinterpret_cast<sockaddr*>(&remote_sockaddr_storage), &remote_sock_len) < 0) {
        throw std::system_error{
            errno,
            std::generic_category()
        };
    }
    m_socket.m_remote_endpoint = detail::sockaddr_to_ip_endpoint(std::ref(*reinterpret_cast<sockaddr*>(&remote_sockaddr_storage)));
}
