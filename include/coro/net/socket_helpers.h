//
// Created by Jesson on 2024/10/11.
//

#ifndef SOCKET_HELPERS_H
#define SOCKET_HELPERS_H

#include "ip_endpoint.h"

#include <netinet/in.h>
#include <functional>

#define SD_RECEIVE SHUT_RD
#define SD_SEND SHUT_WR
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define SOCKADDR_STORAGE struct sockaddr_storage
#define SOCKADDR struct sockaddr
#define SOCKADDR_IN struct sockaddr_in
#define SOCKADDR_IN6 struct sockaddr_in6
#define closesocket(__handle) close((__handle))

namespace coro::net {

class ip_endpoint;

namespace detail {

/// Convert a sockaddr to an IP endpoint.
ip_endpoint sockaddr_to_ip_endpoint(const sockaddr& address) noexcept;

/// Converts an ip_endpoint to a sockaddr structure.
///
/// \param end_point
/// The IP endpoint to convert to a sockaddr structure.
///
/// \param address
/// The sockaddr structure to populate.
///
/// \return
/// The length of the sockaddr structure that was populated.
int ip_endpoint_to_sockaddr(
    const ip_endpoint& end_point, std::reference_wrapper<sockaddr_storage> address) noexcept;

}

}

#endif //SOCKET_HELPERS_H
