//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/net/socket_helpers.h"

coro::net::ip_endpoint coro::net::detail::sockaddr_to_ip_endpoint(const sockaddr& address) noexcept {
    if (address.sa_family == AF_INET) {
        SOCKADDR_IN ipv4_address{};
        std::memcpy(&ipv4_address, &address, sizeof(ipv4_address));

        std::uint8_t address_bytes[4];
        std::memcpy(address_bytes, &ipv4_address.sin_addr, 4);

        return ipv4_endpoint{ipv4_address{address_bytes}, ntohs(ipv4_address.sin_port)};
    }
    assert(address.sa_family == AF_INET6);

    SOCKADDR_IN6 ipv6_address{};
    std::memcpy(&ipv6_address, &address, sizeof(ipv6_address));

    return ipv6_endpoint{ipv6_address{ipv6_address.sin6_addr.s6_addr}, ntohs(ipv6_address.sin6_port)};
}

int coro::net::detail::ip_endpoint_to_sockaddr(
    const ip_endpoint& end_point, std::reference_wrapper<sockaddr_storage> address) noexcept {
    if (end_point.is_ipv4()) {
        const auto& ipv4_endpoint = end_point.to_ipv4();

        SOCKADDR_IN ipv4_address{};
        ipv4_address.sin_family = AF_INET;
        std::memcpy(&ipv4_address.sin_addr, ipv4_endpoint.address().bytes(), 4);
        ipv4_address.sin_port = htons(ipv4_endpoint.port());
        std::memset(&ipv4_address.sin_zero, 0, sizeof(ipv4_address.sin_zero));

        std::memcpy(&address.get(), &ipv4_address, sizeof(ipv4_address));
        return sizeof(SOCKADDR_IN);
    }
    const auto& ipv6_endpoint = end_point.to_ipv6();

    SOCKADDR_IN6 ipv6_address{};
    ipv6_address.sin6_family = AF_INET6;
    std::memcpy(&ipv6_address.sin6_addr, ipv6_endpoint.address().bytes(), 16);
    ipv6_address.sin6_port = htons(ipv6_endpoint.port());
    ipv6_address.sin6_flowinfo = 0;
    ipv6_address.sin6_scope_id = 0;

    std::memcpy(&address.get(), &ipv6_address, sizeof(ipv6_address));
    return sizeof(SOCKADDR_IN6);
}
