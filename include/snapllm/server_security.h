/**
 * @file server_security.h
 * @brief Security policy helpers for the HTTP trust boundary.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace snapllm::security {

constexpr std::size_t kMinimumApiKeyBytes = 32;
constexpr std::size_t kMaximumApiKeyBytes = 4096;

/**
 * Return true only for canonical ASCII loopback host spellings.
 *
 * Accepted forms are localhost (case-insensitive, with one optional trailing
 * dot), dotted-decimal 127/8 IPv4, and ::1 with optional URI brackets. Ports
 * and other authority syntax are not accepted.
 */
bool is_loopback_host(std::string_view host) noexcept;

/**
 * Validate a bind host without resolving it.
 *
 * Accepts ASCII DNS names, IPv4 literals, and IPv6 literals. Shell, URL,
 * whitespace, control, wildcard, and authority characters are rejected.
 */
bool is_valid_bind_host(std::string_view host) noexcept;

/**
 * Validate an HTTP Host value for a server listening on loopback.
 *
 * The value must contain a loopback host and may omit its port. If a port is
 * present, it must equal server_port.
 */
bool is_valid_loopback_host_header(
    std::string_view host_header,
    std::uint16_t server_port) noexcept;

/**
 * Match a serialized browser Origin against an exact allowlist.
 *
 * Origins and allowlist entries must be ASCII scheme-and-authority values
 * without credentials, paths, queries, fragments, wildcards, or controls.
 * Callers should include the server's own serialized origin, approved Tauri
 * origins, and explicitly configured origins in allowed_origins.
 */
bool is_browser_origin_allowed(
    std::string_view origin,
    const std::vector<std::string>& allowed_origins) noexcept;

/**
 * Enforce the API-key representation accepted from environment/CLI/headers.
 *
 * Keys are 32..4096 bytes of visible ASCII excluding whitespace.
 */
bool meets_api_key_policy(std::string_view api_key) noexcept;

/**
 * Accept an explicit operator assertion for a protected non-loopback bind.
 *
 * reverse-proxy means a connection/rate-limiting proxy fronts the listener.
 * loopback-port-publish is only for a container port published on host
 * loopback (for example 127.0.0.1:6930:6930).
 */
bool is_valid_network_guard(std::string_view guard) noexcept;

/**
 * Compare bounded byte strings without content-dependent early exit.
 *
 * Inputs larger than kMaximumApiKeyBytes are rejected. This helper is for API
 * token comparison, not password hashing or general cryptography.
 */
bool constant_time_equal(std::string_view supplied, std::string_view expected) noexcept;

/**
 * Parse a strict Authorization: Bearer value and compare its token.
 *
 * The scheme is ASCII case-insensitive. The expected key must satisfy
 * meets_api_key_policy; malformed values fail closed.
 */
bool bearer_api_key_matches(
    std::string_view authorization_header,
    std::string_view expected_api_key) noexcept;

/**
 * Validate and compare a strict X-API-Key header value.
 *
 * Both the supplied and expected keys must satisfy meets_api_key_policy.
 */
bool x_api_key_matches(
    std::string_view x_api_key_header,
    std::string_view expected_api_key) noexcept;

/**
 * Resolve candidate and return it only when it is contained by root.
 *
 * root must exist and be a directory. weakly_canonical is used so a
 * non-existent final candidate can be validated while existing symlink
 * ancestors are resolved. Windows UNC and device-namespace paths are rejected
 * on every platform. Component comparison prevents string-prefix escapes.
 */
std::optional<std::filesystem::path> canonical_path_within_root(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) noexcept;

}  // namespace snapllm::security
