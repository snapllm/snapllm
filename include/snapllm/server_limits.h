/**
 * @file server_limits.h
 * @brief Resource limits and filesystem policy for server request data.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace snapllm::limits {

constexpr std::int64_t kMinimumMaxTokens = 1;
constexpr std::int64_t kMaximumMaxTokens = 32768;
constexpr std::size_t kMaximumPromptBytes = 1024 * 1024;
constexpr std::size_t kMaximumMessageBytes = 256 * 1024;
constexpr std::size_t kMaximumStringBytes = 64 * 1024;
constexpr std::size_t kMaximumMessages = 256;
constexpr std::size_t kMaximumBatchItems = 64;

constexpr std::int64_t kMinimumImageDimension = 64;
constexpr std::int64_t kMaximumImageDimension = 4096;
constexpr std::uint64_t kMaximumImagePixels = 16ULL * 1024ULL * 1024ULL;
constexpr std::int64_t kMinimumDiffusionSteps = 1;
constexpr std::int64_t kMaximumDiffusionSteps = 150;

constexpr std::size_t kMaximumVisionImages = 8;
constexpr std::size_t kMaximumBase64ImageBytes = 24 * 1024 * 1024;
constexpr std::uintmax_t kMaximumAssetResponseBytes = 32ULL * 1024ULL * 1024ULL;

bool is_valid_max_tokens(std::int64_t value) noexcept;
bool is_valid_prompt_size(std::size_t bytes, bool allow_empty = false) noexcept;
bool is_valid_message_size(std::size_t bytes, bool allow_empty = false) noexcept;
bool is_valid_string_size(std::size_t bytes, bool allow_empty = true) noexcept;
bool is_valid_message_count(std::size_t count) noexcept;
bool is_valid_batch_count(std::size_t count) noexcept;
bool is_valid_identifier_component(std::string_view value) noexcept;
std::optional<std::size_t> find_last_role_index(
    const std::vector<std::string_view>& roles,
    std::string_view target_role) noexcept;

bool is_valid_diffusion_request(
    std::int64_t width,
    std::int64_t height,
    std::int64_t steps) noexcept;
bool is_valid_vision_image_count(std::size_t count) noexcept;
bool is_valid_base64_image_size(std::size_t encoded_bytes) noexcept;
bool is_valid_decoded_image_dimensions(
    std::int64_t width,
    std::int64_t height) noexcept;
std::optional<std::uintmax_t> bounded_regular_file_size(
    const std::filesystem::path& path,
    std::uintmax_t maximum_bytes = kMaximumAssetResponseBytes) noexcept;
std::optional<std::vector<std::uint8_t>> decode_base64_strict(
    std::string_view encoded);

/**
 * Canonicalize a request path and confine it to one of the allowed roots.
 *
 * Relative candidates are interpreted below each root in order. UNC/device
 * paths, missing roots, symlink escapes, and paths outside every root fail
 * closed. The candidate itself need not exist.
 */
std::optional<std::filesystem::path> canonical_path_within_roots(
    const std::vector<std::filesystem::path>& roots,
    const std::filesystem::path& candidate) noexcept;

}  // namespace snapllm::limits
