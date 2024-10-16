//
// Created by Jesson on 2024/10/10.
//

#ifndef IP_ENDPOINT_H
#define IP_ENDPOINT_H

#include "ip_address.h"
#include "ipv4_endpoint.h"
#include "ipv6_endpoint.h"

#include <cassert>
#include <optional>
#include <string>

namespace coro::net {

class ip_endpoint {
public:
	// Constructs to IPv4 end-point 0.0.0.0:0
	ip_endpoint() noexcept : m_family(family::ipv4) {}
    ip_endpoint(ipv4_endpoint endpoint) noexcept : m_family(family::ipv4), m_ipv4(endpoint) {}
    ip_endpoint(ipv6_endpoint endpoint) noexcept : m_family(family::ipv6), m_ipv6(endpoint) {}

	bool is_ipv4() const noexcept { return m_family == family::ipv4; }
	bool is_ipv6() const noexcept { return m_family == family::ipv6; }

    const ipv4_endpoint& to_ipv4() const { assert(is_ipv4());return m_ipv4; }
    const ipv6_endpoint& to_ipv6() const { assert(is_ipv6());return m_ipv6; }

    ip_address address() const noexcept {
	    if (is_ipv4()) {
            return m_ipv4.address();
        }
        return m_ipv6.address();
    }

    std::uint16_t port() const noexcept {
	    return is_ipv4() ? m_ipv4.port() : m_ipv6.port();
	}

    std::string to_string() const {
	    return is_ipv4() ? m_ipv4.to_string() : m_ipv6.to_string();
	}

    static std::optional<ip_endpoint> from_string(const std::string_view string) noexcept {
	    if (auto ipv4 = ipv4_endpoint::from_string(string); ipv4) {
	        return *ipv4;
	    }
	    if (auto ipv6 = ipv6_endpoint::from_string(string); ipv6) {
	        return *ipv6;
	    }
	    return std::nullopt;
	}

    bool operator==(const ip_endpoint& rhs) const noexcept {
        if (is_ipv4()) {
            return rhs.is_ipv4() && m_ipv4 == rhs.m_ipv4;
        }
        return rhs.is_ipv6() && m_ipv6 == rhs.m_ipv6;
    }

    bool operator!=(const ip_endpoint& rhs) const noexcept {
        return !(*this == rhs);
    }

	//  ipv4_endpoint sorts less than ipv6_endpoint
    bool operator<(const ip_endpoint& rhs) const noexcept {
        if (is_ipv4()) {
            return !rhs.is_ipv4() || m_ipv4 < rhs.m_ipv4;
        }
        return rhs.is_ipv6() && m_ipv6 < rhs.m_ipv6;
    }

    bool operator>(const ip_endpoint& rhs) const noexcept {
        return rhs < *this;
    }

    bool operator<=(const ip_endpoint& rhs) const noexcept {
        return rhs >= *this;
    }

    bool operator>=(const ip_endpoint& rhs) const noexcept {
        return !(*this < rhs);
    }

private:
	enum class family { ipv4, ipv6 };
	family m_family;
	union {
		ipv4_endpoint m_ipv4;
		ipv6_endpoint m_ipv6;
	};
};

}

#endif //IP_ENDPOINT_H
