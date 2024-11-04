//
// Created by Jesson on 2024/10/10.
//

#ifndef IPV6_ADDRESS_H
#define IPV6_ADDRESS_H

#include <cstdint>
#include <cassert>
#include <optional>
#include <string>
#include <string_view>

namespace coro::net {

class ipv4_address;

class ipv6_address {
	using bytes_t = std::uint8_t[16];
public:
	constexpr ipv6_address()
        : m_bytes{
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0 } {}

	explicit constexpr ipv6_address(std::uint64_t subnet_prefix, std::uint64_t interface_identifier)
        : m_bytes{
            static_cast<std::uint8_t>(subnet_prefix >> 56),
            static_cast<std::uint8_t>(subnet_prefix >> 48),
            static_cast<std::uint8_t>(subnet_prefix >> 40),
            static_cast<std::uint8_t>(subnet_prefix >> 32),
            static_cast<std::uint8_t>(subnet_prefix >> 24),
            static_cast<std::uint8_t>(subnet_prefix >> 16),
            static_cast<std::uint8_t>(subnet_prefix >> 8),
            static_cast<std::uint8_t>(subnet_prefix),
            static_cast<std::uint8_t>(interface_identifier >> 56),
            static_cast<std::uint8_t>(interface_identifier >> 48),
            static_cast<std::uint8_t>(interface_identifier >> 40),
            static_cast<std::uint8_t>(interface_identifier >> 32),
            static_cast<std::uint8_t>(interface_identifier >> 24),
            static_cast<std::uint8_t>(interface_identifier >> 16),
            static_cast<std::uint8_t>(interface_identifier >> 8),
            static_cast<std::uint8_t>(interface_identifier) } {}

	constexpr ipv6_address(
		std::uint16_t part0,
		std::uint16_t part1,
		std::uint16_t part2,
		std::uint16_t part3,
		std::uint16_t part4,
		std::uint16_t part5,
		std::uint16_t part6,
		std::uint16_t part7)
        : m_bytes{
            static_cast<std::uint8_t>(part0 >> 8),
            static_cast<std::uint8_t>(part0),
            static_cast<std::uint8_t>(part1 >> 8),
            static_cast<std::uint8_t>(part1),
            static_cast<std::uint8_t>(part2 >> 8),
            static_cast<std::uint8_t>(part2),
            static_cast<std::uint8_t>(part3 >> 8),
            static_cast<std::uint8_t>(part3),
            static_cast<std::uint8_t>(part4 >> 8),
            static_cast<std::uint8_t>(part4),
            static_cast<std::uint8_t>(part5 >> 8),
            static_cast<std::uint8_t>(part5),
            static_cast<std::uint8_t>(part6 >> 8),
            static_cast<std::uint8_t>(part6),
            static_cast<std::uint8_t>(part7 >> 8),
            static_cast<std::uint8_t>(part7) } {}

	explicit constexpr ipv6_address(const std::uint16_t(&parts)[8])
        : ipv6_address(
        parts[0], parts[1], parts[2], parts[3],
        parts[4], parts[5], parts[6], parts[7]) {}

	explicit constexpr ipv6_address(const std::uint8_t(&bytes)[16])
        : m_bytes{
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15] } {}

	constexpr const bytes_t& bytes() const { return m_bytes; }

    constexpr std::uint64_t subnet_prefix() const {
	    return
            static_cast<std::uint64_t>(m_bytes[0]) << 56 |
            static_cast<std::uint64_t>(m_bytes[1]) << 48 |
            static_cast<std::uint64_t>(m_bytes[2]) << 40 |
            static_cast<std::uint64_t>(m_bytes[3]) << 32 |
            static_cast<std::uint64_t>(m_bytes[4]) << 24 |
            static_cast<std::uint64_t>(m_bytes[5]) << 16 |
            static_cast<std::uint64_t>(m_bytes[6]) << 8 |
            static_cast<std::uint64_t>(m_bytes[7]);
	}

    constexpr std::uint64_t interface_identifier() const {
	    return
            static_cast<std::uint64_t>(m_bytes[8]) << 56 |
            static_cast<std::uint64_t>(m_bytes[9]) << 48 |
            static_cast<std::uint64_t>(m_bytes[10]) << 40 |
            static_cast<std::uint64_t>(m_bytes[11]) << 32 |
            static_cast<std::uint64_t>(m_bytes[12]) << 24 |
            static_cast<std::uint64_t>(m_bytes[13]) << 16 |
            static_cast<std::uint64_t>(m_bytes[14]) << 8 |
            static_cast<std::uint64_t>(m_bytes[15]);
	}

	/// Get the IPv6 unspedified address :: (all zeroes).
    static constexpr ipv6_address unspecified() { return ipv6_address{}; }

	/// Get the IPv6 loopback address ::1.
    static constexpr ipv6_address loopback() {
	    return ipv6_address{ 0, 0, 0, 0, 0, 0, 0, 1 };
	}

	/// Parse a string representation of an IPv6 address.
	///
	/// \param string
	/// The string to parse.
	/// Must be in ASCII, UTF-8 or Latin-1 encoding.
	///
	/// \return
	/// The IP address if successful, otherwise std::nullopt if the string
	/// could not be parsed as an IPv4 address.
	static std::optional<ipv6_address> from_string(std::string_view string) noexcept {
	    // Longest possible valid IPv6 string is
	    // "xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:nnn.nnn.nnn.nnn"
	    constexpr std::size_t max_length = 45;

	    if (string.empty() || string.length() > max_length) {
		    return std::nullopt;
	    }

	    const std::size_t length = string.length();
	    std::optional<int> double_colon_pos;
	    std::size_t pos = 0;

	    if (length >= 2 && string[0] == ':' && string[1] == ':') {
		    double_colon_pos = 0;
		    pos = 2;
	    }

	    int part_count = 0;
	    std::uint16_t parts[8] = { 0 };

	    while (pos < length && part_count < 8) {
		    std::uint8_t digits[4];
		    int digit_count = 0;
		    auto digit = try_parse_hex_digit(string[pos]);
		    if (!digit) {
			    return std::nullopt;
		    }

		    do {
			    digits[digit_count] = *digit;
			    ++digit_count;
			    ++pos;
		    } while (digit_count < 4 && pos < length && ((digit = try_parse_hex_digit(string[pos]))));

		    // If we're not at the end of the string then there must either be a ':' or a '.' next
		    // followed by the next part.
		    if (pos < length) {
			    // Check if there's room for anything after the separator.
			    if (pos + 1 == length) {
				    return std::nullopt;
			    }

			    if (string[pos] == ':') {
				    ++pos;
				    if (string[pos] == ':') {
					    if (double_colon_pos) {
						    // This is a second double-colon, which is invalid.
						    return std::nullopt;
					    }

					    double_colon_pos = part_count + 1;
					    ++pos;
				    }
			    }
			    else if (string[pos] == '.') {
				    // Treat the current set of digits as decimal digits and parse
				    // the remaining three groups as dotted decimal notation.

				    // Decimal notation produces two 16-bit parts.
				    // If we already have more than 6 parts then we'll end up
				    // with too many.
				    if (part_count > 6) {
					    return std::nullopt;
				    }

				    // Check for over-long or octal notation.
				    if (digit_count > 3 || (digit_count > 1 && digits[0] == 0)) {
					    return std::nullopt;
				    }

				    // Check that digits are valid decimal digits
				    if (digits[0] > 9 || (digit_count > 1 && digits[1] > 9) || (digit_count == 3 && digits[2] > 9)) {
					    return std::nullopt;
				    }

				    std::uint16_t decimal_parts[4];

				    {
					    decimal_parts[0] = digits[0];
					    for (int i = 1; i < digit_count; ++i) {
						    decimal_parts[0] *= 10;
						    decimal_parts[0] += digits[i];
					    }

					    if (decimal_parts[0] > 255) {
						    return std::nullopt;
					    }
				    }

				    for (int decimal_part = 1; decimal_part < 4; ++decimal_part) {
					    if (string[pos] != '.') {
						    return std::nullopt;
					    }

					    ++pos;

					    if (pos == length || !std::isdigit(string[pos])) {
						    // Expected a number after a dot.
						    return std::nullopt;
					    }

					    const bool has_leading_zero = string[pos] == '0';

					    decimal_parts[decimal_part] = digit_value(string[pos]);
					    ++pos;
					    digit_count = 1;
					    while (digit_count < 3 && pos < length && std::isdigit(string[pos])) {
						    decimal_parts[decimal_part] *= 10;
						    decimal_parts[decimal_part] += digit_value(string[pos]);
						    ++pos;
						    ++digit_count;
					    }

					    if (decimal_parts[decimal_part] > 255) {
						    return std::nullopt;
					    }

					    // Detect octal-style number (redundant leading zero)
					    if (digit_count > 1 && has_leading_zero) {
						    return std::nullopt;
					    }
				    }

				    parts[part_count] = (decimal_parts[0] << 8) + decimal_parts[1];
				    parts[part_count + 1] = (decimal_parts[2] << 8) + decimal_parts[3];
				    part_count += 2;

				    // Dotted decimal notation only appears at end.
				    // Don't parse any more of the string.
				    break;
			    }
			    else {
				    // Invalid separator.
				    return std::nullopt;
			    }
		    }

		    // Current part was made up of hex-digits.
		    std::uint16_t part_value = digits[0];
		    for (int i = 1; i < digit_count; ++i) {
			    part_value = part_value * 16 + digits[i];
		    }

		    parts[part_count] = part_value;
		    ++part_count;
	    }

	    // Finished parsing the IPv6 address, we should have consumed all of the string.
	    if (pos < length) {
		    return std::nullopt;
	    }

	    if (part_count < 8) {
		    if (!double_colon_pos) {
			    return std::nullopt;
		    }

		    const int pre_count = *double_colon_pos;
		    const int post_count = part_count - pre_count;
		    const int zero_count = 8 - pre_count - post_count;

		    // Move parts after double colon down to the end.
		    for (int i = 0; i < post_count; ++i) {
			    parts[7 - i] = parts[7 - zero_count - i];
		    }

		    // Fill gap with zeroes.
		    for (int i = 0; i < zero_count; ++i) {
			    parts[pre_count + i] = 0;
		    }
	    }
	    else if (double_colon_pos) {
		    return std::nullopt;
	    }

	    return ipv6_address{ parts };
    }

	/// Convert the IP address to contracted string form.
	///
	/// Address is broken up into 16-bit parts, with each part represended in 1-4
	/// lower-case hexadecimal with leading zeroes omitted. Parts are separated
	/// by separated by a ':'. The longest contiguous run of zero parts is contracted
	/// to "::".
	///
	/// For example:
	/// ipv6_address::unspecified() -> "::"
	/// ipv6_address::loopback() -> "::1"
	/// ipv6_address(0x0011223344556677, 0x8899aabbccddeeff) ->
	///   "11:2233:4455:6677:8899:aabb:ccdd:eeff"
	/// ipv6_address(0x0102030400000000, 0x003fc447ab991011) ->
	///   "102:304::3f:c447:ab99:1011"
    std::string to_string() const {
	    std::uint32_t longest_zero_run_start = 0;
	    std::uint32_t longest_zero_run_length = 0;
	    for (std::uint32_t i = 0; i < 8; ) {
	        if (m_bytes[2 * i] == 0 && m_bytes[2 * i + 1] == 0) {
	            const std::uint32_t zero_run_start = i;
	            ++i;
	            while (i < 8 && m_bytes[2 * i] == 0 && m_bytes[2 * i + 1] == 0) {
	                ++i;
	            }

	            std::uint32_t zero_run_length = i - zero_run_start;
	            if (zero_run_length > longest_zero_run_length) {
	                longest_zero_run_length = zero_run_length;
	                longest_zero_run_start = zero_run_start;
	            }
	        }
	        else {
	            ++i;
	        }
	    }

	    // Longest string will be 8 x 4 digits + 7 ':' separators
	    char buffer[40];

	    char* c = &buffer[0];

	    auto append_part = [&](std::uint32_t index) {
	        const std::uint8_t high_byte = m_bytes[index * 2];
	        const std::uint8_t low_byte = m_bytes[index * 2 + 1];

	        // Don't output leading zero hex digits in the part string.
	        if (high_byte > 0 || low_byte > 15) {
	            if (high_byte > 0) {
	                if (high_byte > 15) {
	                    *c++ = hex_char(high_byte >> 4);
	                }
	                *c++ = hex_char(high_byte & 0xF);
	            }
	            *c++ = hex_char(low_byte >> 4);
	        }
	        *c++ =hex_char(low_byte & 0xF);
	    };

	    if (longest_zero_run_length >= 2) {
	        for (std::uint32_t i = 0; i < longest_zero_run_start; ++i) {
	            if (i > 0) {
	                *c++ = ':';
	            }
	            append_part(i);
	        }
	        *c++ = ':';
	        *c++ = ':';

	        for (std::uint32_t i = longest_zero_run_start + longest_zero_run_length; i < 8; ++i) {
	            append_part(i);
	            if (i < 7) {
	                *c++ = ':';
	            }
	        }
	    }
	    else {
	        append_part(0);
	        for (std::uint32_t i = 1; i < 8; ++i) {
	            *c++ = ':';
	            append_part(i);
	        }
	    }

	    assert((c - &buffer[0]) <= sizeof(buffer));

	    return std::string{ &buffer[0], c };
	}

    constexpr bool operator==(const ipv6_address& other) const {
        for (int i = 0; i < 16; ++i) {
            if (m_bytes[i] != other.m_bytes[i]) return false;
        }
        return true;
    }

    constexpr bool operator!=(const ipv6_address& other) const  {
        return !(*this == other);
    }

    constexpr bool operator<(const ipv6_address& other) const {
        for (int i = 0; i < 16; ++i) {
            if (m_bytes[i] != other.m_bytes[i])
                return m_bytes[i] < other.m_bytes[i];
        }
        return false;
    }

    constexpr bool operator>(const ipv6_address& other) const {
        return other < *this;
    }

    constexpr bool operator<=(const ipv6_address& other) const {
        return other >= *this;
    }

    constexpr bool operator>=(const ipv6_address& other) const {
        return !(*this < other);
    }

private:
    static constexpr std::uint8_t digit_value(const char c) {
        return static_cast<std::uint8_t>(c - '0');
    }

    static std::optional<std::uint8_t> try_parse_hex_digit(const char c) {
        if (c >= '0' && c <= '9') {
            return static_cast<std::uint8_t>(c - '0');
        }
        if (c >= 'a' && c <= 'f') {
            return static_cast<std::uint8_t>(c - 'a' + 10);
        }
        if (c >= 'A' && c <= 'F') {
            return static_cast<std::uint8_t>(c - 'A' + 10);
        }
        return std::nullopt;
    }

    static char hex_char(const std::uint8_t value) {
        return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
    }

	alignas(std::uint64_t) std::uint8_t m_bytes[16];
};

}

#endif //IPV6_ADDRESS_H
