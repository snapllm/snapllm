#include "snapllm/server_limits.h"

#include "snapllm/server_security.h"

#include <limits>

namespace snapllm::limits {
namespace {

bool is_valid_nonempty_size(std::size_t bytes, std::size_t maximum) noexcept {
    return bytes > 0 && bytes <= maximum;
}

bool has_valid_pixel_count(std::int64_t width, std::int64_t height) noexcept {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const auto unsigned_width = static_cast<std::uint64_t>(width);
    const auto unsigned_height = static_cast<std::uint64_t>(height);
    return unsigned_width <=
           (kMaximumImagePixels / unsigned_height);
}

}  // namespace

bool is_valid_max_tokens(std::int64_t value) noexcept {
    return value >= kMinimumMaxTokens && value <= kMaximumMaxTokens;
}

bool is_valid_prompt_size(std::size_t bytes, bool allow_empty) noexcept {
    return (allow_empty && bytes == 0) ||
           is_valid_nonempty_size(bytes, kMaximumPromptBytes);
}

bool is_valid_message_size(std::size_t bytes, bool allow_empty) noexcept {
    return (allow_empty && bytes == 0) ||
           is_valid_nonempty_size(bytes, kMaximumMessageBytes);
}

bool is_valid_string_size(std::size_t bytes, bool allow_empty) noexcept {
    return (allow_empty && bytes == 0) ||
           is_valid_nonempty_size(bytes, kMaximumStringBytes);
}

bool is_valid_message_count(std::size_t count) noexcept {
    return count > 0 && count <= kMaximumMessages;
}

bool is_valid_batch_count(std::size_t count) noexcept {
    return count > 0 && count <= kMaximumBatchItems;
}

bool is_valid_identifier_component(std::string_view value) noexcept {
    if (value.empty() || value.size() > 255) {
        return false;
    }
    for (const unsigned char byte : value) {
        if (byte < 0x20U || byte == 0x7fU || byte == '/' || byte == '\\') {
            return false;
        }
    }
    return true;
}

std::optional<std::size_t> find_last_role_index(
    const std::vector<std::string_view>& roles,
    std::string_view target_role) noexcept {
    for (std::size_t index = roles.size(); index > 0; --index) {
        if (roles[index - 1] == target_role) {
            return index - 1;
        }
    }
    return std::nullopt;
}

bool is_valid_diffusion_request(
    std::int64_t width,
    std::int64_t height,
    std::int64_t steps) noexcept {
    return width >= kMinimumImageDimension &&
           width <= kMaximumImageDimension &&
           height >= kMinimumImageDimension &&
           height <= kMaximumImageDimension &&
           has_valid_pixel_count(width, height) &&
           steps >= kMinimumDiffusionSteps &&
           steps <= kMaximumDiffusionSteps;
}

bool is_valid_vision_image_count(std::size_t count) noexcept {
    return count > 0 && count <= kMaximumVisionImages;
}

bool is_valid_base64_image_size(std::size_t encoded_bytes) noexcept {
    return is_valid_nonempty_size(encoded_bytes, kMaximumBase64ImageBytes);
}

bool is_valid_decoded_image_dimensions(
    std::int64_t width,
    std::int64_t height) noexcept {
    return width > 0 && height > 0 &&
           width <= kMaximumImageDimension &&
           height <= kMaximumImageDimension &&
           has_valid_pixel_count(width, height);
}

std::optional<std::uintmax_t> bounded_regular_file_size(
    const std::filesystem::path& path,
    std::uintmax_t maximum_bytes) noexcept {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum_bytes) {
        return std::nullopt;
    }
    return size;
}

std::optional<std::vector<std::uint8_t>> decode_base64_strict(
    std::string_view encoded) {
    if (encoded.empty() || encoded.size() % 4 != 0) {
        return std::nullopt;
    }
    const auto decode = [](unsigned char value) -> int {
        if (value >= 'A' && value <= 'Z') return value - 'A';
        if (value >= 'a' && value <= 'z') return value - 'a' + 26;
        if (value >= '0' && value <= '9') return value - '0' + 52;
        if (value == '+') return 62;
        if (value == '/') return 63;
        return -1;
    };

    std::vector<std::uint8_t> result;
    result.reserve((encoded.size() / 4) * 3);
    for (std::size_t offset = 0; offset < encoded.size(); offset += 4) {
        const bool final_group = offset + 4 == encoded.size();
        const bool pad2 = encoded[offset + 2] == '=';
        const bool pad3 = encoded[offset + 3] == '=';
        if (!final_group && (pad2 || pad3)) return std::nullopt;
        if (pad2 && !pad3) return std::nullopt;

        const int a = decode(static_cast<unsigned char>(encoded[offset]));
        const int b = decode(static_cast<unsigned char>(encoded[offset + 1]));
        const int c = pad2 ? 0 : decode(static_cast<unsigned char>(encoded[offset + 2]));
        const int d = pad3 ? 0 : decode(static_cast<unsigned char>(encoded[offset + 3]));
        if (a < 0 || b < 0 || c < 0 || d < 0) return std::nullopt;
        if ((pad2 && (b & 0x0f) != 0) || (pad3 && !pad2 && (c & 0x03) != 0)) {
            return std::nullopt;
        }

        result.push_back(static_cast<std::uint8_t>((a << 2) | (b >> 4)));
        if (!pad2) {
            result.push_back(static_cast<std::uint8_t>((b << 4) | (c >> 2)));
        }
        if (!pad3) {
            result.push_back(static_cast<std::uint8_t>((c << 6) | d));
        }
    }
    return result;
}

std::optional<std::filesystem::path> canonical_path_within_roots(
    const std::vector<std::filesystem::path>& roots,
    const std::filesystem::path& candidate) noexcept {
    if (candidate.empty()) {
        return std::nullopt;
    }

    try {
        for (const auto& root : roots) {
            const std::filesystem::path rooted_candidate =
                candidate.is_absolute() ? candidate : root / candidate;
            auto canonical =
                security::canonical_path_within_root(root, rooted_candidate);
            if (canonical) {
                return canonical;
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        return std::nullopt;
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace snapllm::limits
