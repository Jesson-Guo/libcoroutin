//
// Created by Jesson on 2024/10/10.
//

#ifndef IPV4_ENDPOINT_H
#define IPV4_ENDPOINT_H

#include "ipv4_address.h"

#include <optional>
#include <string>
#include <string_view>

namespace coro::net {

class ipv4_endpoint {
public:
    // Construct to 0.0.0.0:0
    ipv4_endpoint() noexcept : m_port(0) {}

    explicit ipv4_endpoint(ipv4_address address, std::uint16_t port = 0) noexcept
        : m_address(address)
        , m_port(port) {}

    const ipv4_address& address() const noexcept { return m_address; }

    std::uint16_t port() const noexcept { return m_port; }

    std::string to_string() const {
        auto s = m_address.to_string();
        s.push_back(':');
        s.append(std::to_string(m_port));
        return s;
    }

    static std::optional<ipv4_endpoint> from_string(std::string_view string) noexcept {
        const auto colon_pos = string.find(':');
        if (colon_pos == std::string_view::npos) {
            return std::nullopt;
        }

        const auto address = ipv4_address::from_string(string.substr(0, colon_pos));
        if (!address) {
            return std::nullopt;
        }

        const auto port = parse_port(string.substr(colon_pos + 1));
        if (!port) {
            return std::nullopt;
        }

        return ipv4_endpoint{ *address, *port };
    }

private:
    static std::uint8_t digit_value(const char c) {
        return static_cast<std::uint8_t>(c - '0');
    }

    static std::optional<std::uint16_t> parse_port(const std::string_view string) {
        if (string.empty()) return std::nullopt;

        std::uint32_t value = 0;
        for (const auto c : string) {
            if (!std::isdigit(c)) return std::nullopt;
            value = value * 10 + digit_value(c);
            if (value > 0xFFFFu) return std::nullopt;
        }

        return static_cast<std::uint16_t>(value);
    }

    ipv4_address m_address;
    std::uint16_t m_port;
};

inline bool operator==(const ipv4_endpoint& a, const ipv4_endpoint& b) {
    return a.address() == b.address() && a.port() == b.port();
}

inline bool operator!=(const ipv4_endpoint& a, const ipv4_endpoint& b) {
    return !(a == b);
}

inline bool operator<(const ipv4_endpoint& a, const ipv4_endpoint& b) {
    return a.address() < b.address() ||
        (a.address() == b.address() && a.port() < b.port());
}

inline bool operator>(const ipv4_endpoint& a, const ipv4_endpoint& b) {
    return b < a;
}

inline bool operator<=(const ipv4_endpoint& a, const ipv4_endpoint& b) {
    return !(b < a);
}

inline bool operator>=(const ipv4_endpoint& a, const ipv4_endpoint& b) {
    return !(a < b);
}

}

#endif //IPV4_ENDPOINT_H
