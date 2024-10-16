//
// Created by Jesson on 2024/10/10.
//

#ifndef IPV6_ENDPOINT_H
#define IPV6_ENDPOINT_H

#include "ipv6_address.h"

#include <optional>
#include <string>
#include <string_view>

namespace coro::net {

class ipv6_endpoint {
public:
    // Construct to [::]:0
    ipv6_endpoint() noexcept : m_port(0) {}

    explicit ipv6_endpoint(ipv6_address address, std::uint16_t port = 0) noexcept
        : m_address(address)
        , m_port(port) {}

    const ipv6_address& address() const noexcept { return m_address; }

    std::uint16_t port() const noexcept { return m_port; }

    std::string to_string() const {
        std::string result;
        result.push_back('[');
        result += m_address.to_string();
        result += "]:";
        result += std::to_string(m_port);
        return result;
    }

    static std::optional<ipv6_endpoint> from_string(std::string_view string) noexcept {
        // Shortest valid endpoint is "[::]:0"
        if (string.size() < 6) {
            return std::nullopt;
        }

        if (string[0] != '[') {
            return std::nullopt;
        }

        const auto close_bracket_pos = string.find("]:", 1);
        if (close_bracket_pos == std::string_view::npos) {
            return std::nullopt;
        }

        const auto address = ipv6_address::from_string(string.substr(1, close_bracket_pos - 1));
        if (!address) {
            return std::nullopt;
        }

        const auto port = parse_port(string.substr(close_bracket_pos + 2));
        if (!port) {
            return std::nullopt;
        }

        return ipv6_endpoint{ *address, *port };
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

    ipv6_address m_address;
    std::uint16_t m_port;
};

inline bool operator==(const ipv6_endpoint& a, const ipv6_endpoint& b) {
    return a.address() == b.address() && a.port() == b.port();
}

inline bool operator!=(const ipv6_endpoint& a, const ipv6_endpoint& b) {
    return !(a == b);
}

inline bool operator<(const ipv6_endpoint& a, const ipv6_endpoint& b) {
    return a.address() < b.address() ||
        (a.address() == b.address() && a.port() < b.port());
}

inline bool operator>(const ipv6_endpoint& a, const ipv6_endpoint& b) {
    return b < a;
}

inline bool operator<=(const ipv6_endpoint& a, const ipv6_endpoint& b) {
    return !(b < a);
}

inline bool operator>=(const ipv6_endpoint& a, const ipv6_endpoint& b) {
    return !(a < b);
}

}

#endif //IPV6_ENDPOINT_H
