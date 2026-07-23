#include "snapllm/server_security.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <system_error>

namespace snapllm::security {
namespace {

constexpr std::size_t kMaximumHostBytes = 253;
constexpr std::size_t kMaximumOriginBytes = 2048;

bool is_visible_ascii(char value) noexcept {
    const auto byte = static_cast<unsigned char>(value);
    return byte >= 0x21U && byte <= 0x7eU;
}

bool is_ascii_text(std::string_view value, std::size_t maximum_bytes) noexcept {
    if (value.empty() || value.size() > maximum_bytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), is_visible_ascii);
}

char ascii_lower(char value) noexcept {
    const auto byte = static_cast<unsigned char>(value);
    if (byte >= static_cast<unsigned char>('A') &&
        byte <= static_cast<unsigned char>('Z')) {
        return static_cast<char>(byte + ('a' - 'A'));
    }
    return value;
}

bool ascii_case_equal(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return false;
        }
    }
    return true;
}

bool parse_decimal_port(std::string_view text, std::uint16_t& port) noexcept {
    if (text.empty() || text.size() > 5) {
        return false;
    }

    unsigned int value = 0;
    for (char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        value = (value * 10U) + static_cast<unsigned int>(character - '0');
        if (value > std::numeric_limits<std::uint16_t>::max()) {
            return false;
        }
    }

    if (value == 0) {
        return false;
    }
    port = static_cast<std::uint16_t>(value);
    return true;
}

bool is_ipv4_loopback(std::string_view host) noexcept {
    std::array<unsigned int, 4> octets{};
    std::size_t octet_index = 0;
    std::size_t start = 0;

    while (start <= host.size() && octet_index < octets.size()) {
        const std::size_t end = host.find('.', start);
        const std::size_t length =
            (end == std::string_view::npos ? host.size() : end) - start;
        if (length == 0 || length > 3 ||
            (length > 1 && host[start] == '0')) {
            return false;
        }

        unsigned int value = 0;
        for (std::size_t offset = 0; offset < length; ++offset) {
            const char character = host[start + offset];
            if (character < '0' || character > '9') {
                return false;
            }
            value = (value * 10U) + static_cast<unsigned int>(character - '0');
        }
        if (value > 255U) {
            return false;
        }

        octets[octet_index++] = value;
        if (end == std::string_view::npos) {
            start = host.size() + 1;
        } else {
            start = end + 1;
        }
    }

    return octet_index == octets.size() && start > host.size() &&
           octets[0] == 127U;
}

bool parse_origin(std::string_view origin) noexcept {
    if (!is_ascii_text(origin, kMaximumOriginBytes)) {
        return false;
    }

    const std::size_t separator = origin.find("://");
    if (separator == std::string_view::npos || separator == 0) {
        return false;
    }

    const std::string_view scheme = origin.substr(0, separator);
    if (!std::isalpha(static_cast<unsigned char>(scheme.front()))) {
        return false;
    }
    for (char character : scheme.substr(1)) {
        const auto byte = static_cast<unsigned char>(character);
        if (!std::isalnum(byte) && character != '+' && character != '-' &&
            character != '.') {
            return false;
        }
    }

    const std::string_view authority = origin.substr(separator + 3);
    if (authority.empty() ||
        authority.find_first_of("/?#@") != std::string_view::npos) {
        return false;
    }

    std::string_view host = authority;
    std::string_view port;
    bool has_port = false;
    if (authority.front() == '[') {
        const std::size_t closing_bracket = authority.find(']');
        if (closing_bracket == std::string_view::npos || closing_bracket == 1) {
            return false;
        }
        host = authority.substr(1, closing_bracket - 1);
        const std::string_view suffix = authority.substr(closing_bracket + 1);
        if (!suffix.empty()) {
            if (suffix.front() != ':') {
                return false;
            }
            has_port = true;
            port = suffix.substr(1);
        }
    } else {
        const std::size_t colon = authority.rfind(':');
        if (colon != std::string_view::npos) {
            if (authority.find(':') != colon) {
                return false;
            }
            has_port = true;
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1);
        }
    }

    if (host.empty() || host.find('*') != std::string_view::npos) {
        return false;
    }
    for (char character : host) {
        const auto byte = static_cast<unsigned char>(character);
        if (!std::isalnum(byte) && character != '-' && character != '.' &&
            character != ':') {
            return false;
        }
    }

    if (has_port) {
        std::uint16_t parsed_port = 0;
        if (!parse_decimal_port(port, parsed_port)) {
            return false;
        }
    }
    return true;
}

bool has_windows_unc_or_device_prefix(const std::filesystem::path& path) {
    const std::string value = path.generic_u8string();
    if (value.size() < 2) {
        return false;
    }

    const bool has_double_slash =
        (value[0] == '/' && value[1] == '/') ||
        (value[0] == '\\' && value[1] == '\\');
    if (!has_double_slash) {
        return false;
    }

    // All UNC paths, including the \\?\ and \\.\ device namespaces, fail closed.
    return true;
}

bool path_component_equal(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
#ifdef _WIN32
    const std::wstring left_value = left.native();
    const std::wstring right_value = right.native();
    if (left_value.size() != right_value.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left_value.size(); ++index) {
        const wchar_t left_character = left_value[index];
        const wchar_t right_character = right_value[index];
        if (left_character == right_character) {
            continue;
        }
        if (left_character >= L'A' && left_character <= L'Z' &&
            left_character + (L'a' - L'A') == right_character) {
            continue;
        }
        if (right_character >= L'A' && right_character <= L'Z' &&
            right_character + (L'a' - L'A') == left_character) {
            continue;
        }
        return false;
    }
    return true;
#else
    return left == right;
#endif
}

bool is_component_prefix(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
    auto root_component = root.begin();
    auto candidate_component = candidate.begin();
    for (; root_component != root.end();
         ++root_component, ++candidate_component) {
        if (candidate_component == candidate.end() ||
            !path_component_equal(*root_component, *candidate_component)) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool is_loopback_host(std::string_view host) noexcept {
    if (!is_ascii_text(host, kMaximumHostBytes)) {
        return false;
    }

    if (host.front() == '[' && host.back() == ']') {
        host.remove_prefix(1);
        host.remove_suffix(1);
    }

    if (ascii_case_equal(host, "localhost") ||
        ascii_case_equal(host, "localhost.")) {
        return true;
    }
    if (host == "::1") {
        return true;
    }
    return is_ipv4_loopback(host);
}

bool is_valid_bind_host(std::string_view host) noexcept {
    if (!is_ascii_text(host, kMaximumHostBytes)) {
        return false;
    }
    if (host.front() == '[' && host.back() == ']') {
        host.remove_prefix(1);
        host.remove_suffix(1);
    }
    if (host.empty() || host.front() == '.' || host.back() == '.' ||
        host.front() == '-' || host.back() == '-') {
        return false;
    }

    bool has_alphanumeric = false;
    for (char character : host) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte)) {
            has_alphanumeric = true;
            continue;
        }
        if (character != '.' && character != '-' && character != ':') {
            return false;
        }
    }
    return has_alphanumeric;
}

bool is_valid_loopback_host_header(
    std::string_view host_header,
    std::uint16_t server_port) noexcept {
    if (!is_ascii_text(host_header, kMaximumHostBytes) || server_port == 0) {
        return false;
    }

    std::string_view host = host_header;
    std::string_view port;
    bool has_port = false;
    if (host_header.front() == '[') {
        const std::size_t closing_bracket = host_header.find(']');
        if (closing_bracket == std::string_view::npos) {
            return false;
        }
        host = host_header.substr(0, closing_bracket + 1);
        const std::string_view suffix = host_header.substr(closing_bracket + 1);
        if (!suffix.empty()) {
            if (suffix.front() != ':') {
                return false;
            }
            has_port = true;
            port = suffix.substr(1);
        }
    } else {
        const std::size_t colon = host_header.rfind(':');
        if (colon != std::string_view::npos) {
            if (host_header.find(':') != colon) {
                return false;
            }
            has_port = true;
            host = host_header.substr(0, colon);
            port = host_header.substr(colon + 1);
        }
    }

    if (!is_loopback_host(host)) {
        return false;
    }
    if (!has_port) {
        return true;
    }

    std::uint16_t parsed_port = 0;
    return parse_decimal_port(port, parsed_port) && parsed_port == server_port;
}

bool is_browser_origin_allowed(
    std::string_view origin,
    const std::vector<std::string>& allowed_origins) noexcept {
    if (!parse_origin(origin)) {
        return false;
    }

    for (const std::string& allowed_origin : allowed_origins) {
        if (parse_origin(allowed_origin) && origin == allowed_origin) {
            return true;
        }
    }
    return false;
}

bool meets_api_key_policy(std::string_view api_key) noexcept {
    if (api_key.size() < kMinimumApiKeyBytes ||
        api_key.size() > kMaximumApiKeyBytes) {
        return false;
    }
    return std::all_of(api_key.begin(), api_key.end(), is_visible_ascii);
}

bool is_valid_network_guard(std::string_view guard) noexcept {
    return guard == "reverse-proxy" || guard == "loopback-port-publish";
}

bool constant_time_equal(
    std::string_view supplied,
    std::string_view expected) noexcept {
    if (supplied.size() > kMaximumApiKeyBytes ||
        expected.size() > kMaximumApiKeyBytes) {
        return false;
    }

    volatile unsigned int difference =
        static_cast<unsigned int>(supplied.size() ^ expected.size());
    for (std::size_t index = 0; index < kMaximumApiKeyBytes; ++index) {
        const unsigned char supplied_byte =
            index < supplied.size()
                ? static_cast<unsigned char>(supplied[index])
                : 0U;
        const unsigned char expected_byte =
            index < expected.size()
                ? static_cast<unsigned char>(expected[index])
                : 0U;
        difference |= static_cast<unsigned int>(supplied_byte ^ expected_byte);
    }
    return difference == 0U;
}

bool bearer_api_key_matches(
    std::string_view authorization_header,
    std::string_view expected_api_key) noexcept {
    constexpr std::string_view prefix = "Bearer ";
    if (!meets_api_key_policy(expected_api_key) ||
        authorization_header.size() <= prefix.size() ||
        !ascii_case_equal(
            authorization_header.substr(0, prefix.size()),
            prefix)) {
        return false;
    }

    const std::string_view supplied_api_key =
        authorization_header.substr(prefix.size());
    return meets_api_key_policy(supplied_api_key) &&
           constant_time_equal(supplied_api_key, expected_api_key);
}

bool x_api_key_matches(
    std::string_view x_api_key_header,
    std::string_view expected_api_key) noexcept {
    return meets_api_key_policy(expected_api_key) &&
           meets_api_key_policy(x_api_key_header) &&
           constant_time_equal(x_api_key_header, expected_api_key);
}

std::optional<std::filesystem::path> canonical_path_within_root(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) noexcept {
    try {
        if (root.empty() || candidate.empty() ||
            has_windows_unc_or_device_prefix(root) ||
            has_windows_unc_or_device_prefix(candidate)) {
            return std::nullopt;
        }

        std::error_code error;
        const std::filesystem::path canonical_root =
            std::filesystem::weakly_canonical(root, error);
        if (error || canonical_root.empty()) {
            return std::nullopt;
        }
        if (!std::filesystem::is_directory(canonical_root, error) || error) {
            return std::nullopt;
        }

        const std::filesystem::path canonical_candidate =
            std::filesystem::weakly_canonical(candidate, error);
        if (error || canonical_candidate.empty() ||
            !is_component_prefix(canonical_root, canonical_candidate)) {
            return std::nullopt;
        }
        return canonical_candidate;
    } catch (const std::filesystem::filesystem_error&) {
        return std::nullopt;
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
}

}  // namespace snapllm::security
