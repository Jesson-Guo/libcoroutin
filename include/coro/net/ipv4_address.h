//
// Created by Jesson on 2024/10/10.
//

#ifndef IPV4_ADDRESS_H
#define IPV4_ADDRESS_H

#include <cstdint>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace coro::net {

class ipv4_address {
	using bytes_t = std::uint8_t[4];

public:
	constexpr ipv4_address() : m_bytes{ 0, 0, 0, 0 } {}

	explicit constexpr ipv4_address(std::uint32_t integer)
		: m_bytes{
			static_cast<std::uint8_t>(integer >> 24),
			static_cast<std::uint8_t>(integer >> 16),
			static_cast<std::uint8_t>(integer >> 8),
			static_cast<std::uint8_t>(integer)
		} {}

	explicit constexpr ipv4_address(const std::uint8_t(&bytes)[4])
		: m_bytes{ bytes[0], bytes[1], bytes[2], bytes[3] } {}

	explicit constexpr ipv4_address(
		std::uint8_t b0, std::uint8_t b1, std::uint8_t b2, std::uint8_t b3)
		: m_bytes{ b0, b1, b2, b3 } {}

	constexpr const bytes_t& bytes() const { return m_bytes; }

	constexpr std::uint32_t to_integer() const {
		return static_cast<std::uint32_t>(m_bytes[0]) << 24 |
			   static_cast<std::uint32_t>(m_bytes[1]) << 16 |
			   static_cast<std::uint32_t>(m_bytes[2]) << 8 |
			   static_cast<std::uint32_t>(m_bytes[3]);
	}

	static constexpr ipv4_address loopback() {
		return ipv4_address(127, 0, 0, 1);
	}

	constexpr bool is_loopback() const {
		return m_bytes[0] == 127;
	}

	constexpr bool is_private_network() const {
		return m_bytes[0] == 10 ||
			(m_bytes[0] == 172 && (m_bytes[1] & 0xF0) == 0x10) ||
			(m_bytes[0] == 192 && m_bytes[2] == 168);
	}

	constexpr bool operator==(const ipv4_address other) const {
		return m_bytes[0] == other.m_bytes[0] &&
			   m_bytes[1] == other.m_bytes[1] &&
			   m_bytes[2] == other.m_bytes[2] &&
			   m_bytes[3] == other.m_bytes[3];
	}

	constexpr bool operator!=(const ipv4_address other) const {return !(*this == other);}
	constexpr bool operator<(const ipv4_address other) const {return to_integer() < other.to_integer();}
	constexpr bool operator>(const ipv4_address other) const {return other < *this;}
	constexpr bool operator<=(const ipv4_address other) const {return other >= *this;}
	constexpr bool operator>=(const ipv4_address other) const {return !(*this < other);}

	/// Parse a string representation of an IP address.
	///
	/// Parses strings of the form:
	/// - "num.num.num.num" where num is an integer in range [0, 255].
	/// - A single integer value in range [0, 2^32).
	///
	/// \param string
	/// The string to parse.
	/// Must be in ASCII, UTF-8 or Latin-1 encoding.
	///
	/// \return
	/// The IP address if successful, otherwise std::nullopt if the string
	/// could not be parsed as an IPv4 address.
	static std::optional<ipv4_address> from_string(std::string_view string) noexcept {
	    if (string.empty()) {
	        return std::nullopt;
	    }

	    if (!std::isdigit(string[0])) {
		    return std::nullopt;
	    }

	    const auto length = string.length();

	    std::uint8_t part_values[4];

	    if (string[0] == '0' && length > 1) {
		    if (std::isdigit(string[1])) {
			    // Octal format (not supported)
			    return std::nullopt;
		    }
		    else if (string[1] == 'x')
		    {
			    // Hexadecimal format (not supported)
			    return std::nullopt;
		    }
	    }

	    // Parse the first integer.
	    // Could be a single 32-bit integer or first integer in a dotted decimal string.
	    std::size_t pos = 0;

	    {
		    constexpr std::uint32_t max_value = 0xFFFFFFFFu / 10;
		    constexpr std::uint32_t max_digit = 0xFFFFFFFFu % 10;

		    std::uint32_t part_value = static_cast<std::uint8_t>(string[pos] - '0');
		    ++pos;

		    while (pos < length && std::isdigit(string[pos])) {
			    const auto digit_value = static_cast<std::uint8_t>(string[pos] - '0');
			    ++pos;

			    // Check if this digit would overflow the 32-bit integer
			    if (part_value > max_value || (part_value == max_value && digit_value > max_digit)) {
				    return std::nullopt;
			    }

			    part_value = (part_value * 10) + digit_value;
		    }
		    if (pos == length) {
			    // A single-integer string
			    return ipv4_address{ part_value };
		    }
		    if (part_value > 255) {
			    // Not a valid first component of dotted decimal
			    return std::nullopt;
		    }
		    part_values[0] = static_cast<std::uint8_t>(part_value);
	    }

	    for (int part = 1; part < 4; ++part) {
		    if ((pos + 1) >= length || string[pos] != '.' || !std::isdigit(string[pos + 1])) {
			    return std::nullopt;
		    }

		    // Skip the '.'
		    ++pos;

		    // Check for an octal format (not yet supported)
            if (pos + 1 < length && string[pos] == '0' && std::isdigit(string[pos + 1])) {
			    return std::nullopt;
		    }

		    std::uint32_t part_value = static_cast<std::uint8_t>(string[pos] - '0');
		    ++pos;
		    if (pos < length && std::isdigit(string[pos])) {
			    part_value = (part_value * 10) + static_cast<std::uint8_t>(string[pos] - '0');
			    ++pos;
			    if (pos < length && std::isdigit(string[pos])) {
				    part_value = (part_value * 10) + static_cast<std::uint8_t>(string[pos] - '0');
				    if (part_value > 255) {
					    return std::nullopt;
				    }
				    ++pos;
			    }
		    }
		    part_values[part] = static_cast<std::uint8_t>(part_value);
	    }

	    if (pos < length) {
		    // Extra chars after end of a valid IPv4 string
		    return std::nullopt;
	    }

	    return ipv4_address{ part_values };
	}

	/// Convert the IP address to dotted decimal notation.
	///
	/// eg. "12.67.190.23"
	std::string to_string() const {
	    // Buffer is large enough to hold larges ip address
	    // "xxx.xxx.xxx.xxx"
	    char buffer[15];

	    char* c = &buffer[0];
	    for (int i = 0; i < 4; ++i) {
	        if (i > 0) {
	            *c++ = '.';
	        }

	        if (m_bytes[i] >= 100) {
	            *c++ = '0' + (m_bytes[i] / 100);
	            *c++ = '0' + (m_bytes[i] % 100) / 10;
	            *c++ = '0' + (m_bytes[i] % 10);
	        }
	        else if (m_bytes[i] >= 10) {
	            *c++ = '0' + (m_bytes[i] / 10);
	            *c++ = '0' + (m_bytes[i] % 10);
	        }
	        else {
	            *c++ = '0' + m_bytes[i];
	        }
	    }

	    return std::string{ &buffer[0], c };
	}

private:
	alignas(std::uint32_t) std::uint8_t m_bytes[4];
};

}

#endif //IPV4_ADDRESS_H
