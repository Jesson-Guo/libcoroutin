//
// Created by Jesson on 2024/10/10.
//

#ifndef IP_ADDRESS_H
#define IP_ADDRESS_H

#include "ipv4_address.h"
#include "ipv6_address.h"

#include <cassert>
#include <optional>
#include <string>

namespace coro::net {

class ip_address {
public:
	// Constructs to IPv4 address 0.0.0.0
    ip_address() noexcept : m_family(family::ipv4), m_ipv4() {}
    ip_address(ipv4_address address) noexcept : m_family(family::ipv4) , m_ipv4(address) {}
    ip_address(ipv6_address address) noexcept : m_family(family::ipv6) , m_ipv6(address) {}

	bool is_ipv4() const noexcept { return m_family == family::ipv4; }
	bool is_ipv6() const noexcept { return m_family == family::ipv6; }

    const ipv4_address& to_ipv4() const {
        assert(is_ipv4());
        return m_ipv4;
    }

    const ipv6_address& to_ipv6() const {
        assert(is_ipv6());
        return m_ipv6;
    }

    const std::uint8_t* bytes() const noexcept {
        return is_ipv4() ? m_ipv4.bytes() : m_ipv6.bytes();
    }

    std::string to_string() const {
        return is_ipv4() ? m_ipv4.to_string() : m_ipv6.to_string();
    }

    static std::optional<ip_address> from_string(const std::string_view string) noexcept {
        if (auto ipv4 = ipv4_address::from_string(string); ipv4) {
            return std::optional<ip_address>(*ipv4);
        }

        if (auto ipv6 = ipv6_address::from_string(string); ipv6) {
            return std::optional<ip_address>(*ipv6);
        }

        return std::nullopt;
    }

    bool operator==(const ip_address& rhs) const noexcept {
        if (is_ipv4()) {
            return rhs.is_ipv4() && m_ipv4 == rhs.m_ipv4;
        }
        return rhs.is_ipv6() && m_ipv6 == rhs.m_ipv6;
    }

    bool operator!=(const ip_address& rhs) const noexcept {
        return !(*this == rhs);
    }

	//  ipv4_address sorts less than ipv6_address
    bool operator<(const ip_address& rhs) const noexcept {
        if (is_ipv4()) {
            return !rhs.is_ipv4() || m_ipv4 < rhs.m_ipv4;
        }
        return rhs.is_ipv6() && m_ipv6 < rhs.m_ipv6;
    }

    bool operator>(const ip_address& rhs) const noexcept {
        return rhs < *this;
    }

    bool operator<=(const ip_address& rhs) const noexcept {
        return rhs >= *this;
    }

    bool operator>=(const ip_address& rhs) const noexcept {
        return !(*this < rhs);
    }

private:
	enum class family {
		ipv4,
		ipv6
	};

	family m_family;

	union {
		ipv4_address m_ipv4;
		ipv6_address m_ipv6;
	};
};

}

#endif //IP_ADDRESS_H
