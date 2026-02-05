/**
 * @file compression.h
 * @brief Compression Utilities for vPID L2 KV Cache Storage
 *
 * Provides compression/decompression support for KV cache persistence:
 * - LZ4: Fast compression, moderate ratio (~2-3x)
 * - ZSTD: High compression ratio (~4-6x), moderate speed
 * - None: No compression (fastest I/O)
 *
 * Design:
 * - Streaming API for large data
 * - In-memory API for small/medium data
 * - Automatic format detection on decompression
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <optional>
#include <memory>
#include <functional>

namespace snapllm {

/**
 * @brief Compression algorithm enumeration
 */
enum class CompressionType {
    None = 0,       ///< No compression
    LZ4 = 1,        ///< LZ4 fast compression
    LZ4_HC = 2,     ///< LZ4 high compression
    ZSTD = 3,       ///< Zstandard compression
    ZSTD_FAST = 4   ///< Zstandard fast mode
};

/**
 * @brief Convert CompressionType to string
 */
inline const char* compression_type_to_string(CompressionType type) {
    switch (type) {
        case CompressionType::None: return "None";
        case CompressionType::LZ4: return "LZ4";
        case CompressionType::LZ4_HC: return "LZ4_HC";
        case CompressionType::ZSTD: return "ZSTD";
        case CompressionType::ZSTD_FAST: return "ZSTD_FAST";
        default: return "Unknown";
    }
}

/**
 * @brief Compression configuration
 */
struct CompressionConfig {
    CompressionType type = CompressionType::LZ4;
    int level = 0;              ///< Compression level (0 = default)
    size_t block_size = 0;      ///< Block size for streaming (0 = auto)

    // LZ4 defaults
    static CompressionConfig lz4() {
        CompressionConfig config;
        config.type = CompressionType::LZ4;
        config.level = 1;  // LZ4 default
        return config;
    }

    static CompressionConfig lz4_hc(int level = 9) {
        CompressionConfig config;
        config.type = CompressionType::LZ4_HC;
        config.level = level;  // 1-12, default 9
        return config;
    }

    // ZSTD defaults
    static CompressionConfig zstd(int level = 3) {
        CompressionConfig config;
        config.type = CompressionType::ZSTD;
        config.level = level;  // 1-22, default 3
        return config;
    }

    static CompressionConfig zstd_fast() {
        CompressionConfig config;
        config.type = CompressionType::ZSTD_FAST;
        config.level = 1;
        return config;
    }

    static CompressionConfig none() {
        CompressionConfig config;
        config.type = CompressionType::None;
        return config;
    }
};

/**
 * @brief Compression result
 */
struct CompressionResult {
    bool success = false;
    std::string error_message;

    std::vector<uint8_t> data;
    size_t original_size = 0;
    size_t compressed_size = 0;

    double ratio() const {
        return compressed_size > 0 ?
            static_cast<double>(original_size) / compressed_size : 1.0;
    }

    double time_ms = 0.0;

    static CompressionResult ok(std::vector<uint8_t>&& data, size_t orig_size, double time) {
        CompressionResult result;
        result.success = true;
        result.compressed_size = data.size();
        result.original_size = orig_size;
        result.data = std::move(data);
        result.time_ms = time;
        return result;
    }

    static CompressionResult fail(const std::string& msg) {
        CompressionResult result;
        result.success = false;
        result.error_message = msg;
        return result;
    }
};

/**
 * @brief Decompression result
 */
struct DecompressionResult {
    bool success = false;
    std::string error_message;

    std::vector<uint8_t> data;
    size_t decompressed_size = 0;
    double time_ms = 0.0;

    static DecompressionResult ok(std::vector<uint8_t>&& data, double time) {
        DecompressionResult result;
        result.success = true;
        result.decompressed_size = data.size();
        result.data = std::move(data);
        result.time_ms = time;
        return result;
    }

    static DecompressionResult fail(const std::string& msg) {
        DecompressionResult result;
        result.success = false;
        result.error_message = msg;
        return result;
    }
};

/**
 * @brief Compressed data header (prepended to compressed data)
 *
 * Layout (16 bytes):
 * - magic[4]: "SCMP" (SnapLLM Compressed)
 * - version: uint8
 * - type: uint8 (CompressionType)
 * - flags: uint16
 * - original_size: uint64
 */
struct CompressedHeader {
    static constexpr char MAGIC[4] = {'S', 'C', 'M', 'P'};
    static constexpr uint8_t VERSION = 1;

    char magic[4] = {'S', 'C', 'M', 'P'};
    uint8_t version = VERSION;
    uint8_t type = 0;
    uint16_t flags = 0;
    uint64_t original_size = 0;

    bool is_valid() const {
        return magic[0] == MAGIC[0] && magic[1] == MAGIC[1] &&
               magic[2] == MAGIC[2] && magic[3] == MAGIC[3];
    }

    CompressionType get_type() const {
        return static_cast<CompressionType>(type);
    }

    void set_type(CompressionType t) {
        type = static_cast<uint8_t>(t);
    }
};

static_assert(sizeof(CompressedHeader) == 16, "CompressedHeader must be 16 bytes");

/**
 * @brief Compression utility class
 *
 * Thread-safe compression/decompression operations.
 *
 * Usage:
 * @code
 * Compressor compressor;
 *
 * // Compress data
 * auto result = compressor.compress(data, CompressionConfig::zstd());
 * if (result.success) {
 *     // result.data contains compressed bytes with header
 *     std::cout << "Ratio: " << result.ratio() << "x" << std::endl;
 * }
 *
 * // Decompress (auto-detects format from header)
 * auto decompressed = compressor.decompress(result.data);
 * @endcode
 */
class Compressor {
public:
    Compressor();
    ~Compressor();

    // Non-copyable
    Compressor(const Compressor&) = delete;
    Compressor& operator=(const Compressor&) = delete;

    //=========================================================================
    // Compression
    //=========================================================================

    /**
     * @brief Compress data with specified configuration
     * @param data Input data
     * @param config Compression configuration
     * @return Compression result with header prepended
     */
    CompressionResult compress(
        const std::vector<uint8_t>& data,
        const CompressionConfig& config = CompressionConfig::lz4()
    );

    /**
     * @brief Compress data from raw pointer
     * @param data Pointer to input data
     * @param size Size in bytes
     * @param config Compression configuration
     * @return Compression result
     */
    CompressionResult compress(
        const void* data,
        size_t size,
        const CompressionConfig& config = CompressionConfig::lz4()
    );

    //=========================================================================
    // Decompression
    //=========================================================================

    /**
     * @brief Decompress data (auto-detects format from header)
     * @param data Compressed data with header
     * @return Decompression result
     */
    DecompressionResult decompress(const std::vector<uint8_t>& data);

    /**
     * @brief Decompress data from raw pointer
     * @param data Pointer to compressed data
     * @param size Size in bytes
     * @return Decompression result
     */
    DecompressionResult decompress(const void* data, size_t size);

    //=========================================================================
    // Utilities
    //=========================================================================

    /**
     * @brief Get maximum compressed size bound
     * @param input_size Input data size
     * @param type Compression type
     * @return Maximum possible compressed size
     */
    static size_t max_compressed_size(size_t input_size, CompressionType type);

    /**
     * @brief Check if data has compression header
     * @param data Data to check
     * @param size Size in bytes
     * @return true if has valid header
     */
    static bool has_header(const void* data, size_t size);

    /**
     * @brief Read compression header from data
     * @param data Data with header
     * @param size Size in bytes
     * @return Header if valid
     */
    static std::optional<CompressedHeader> read_header(const void* data, size_t size);

    /**
     * @brief Check if compression type is available
     * @param type Compression type
     * @return true if library is linked
     */
    static bool is_available(CompressionType type);

private:
    // Internal implementation
    CompressionResult compress_lz4(const void* data, size_t size, int level);
    CompressionResult compress_lz4_hc(const void* data, size_t size, int level);
    CompressionResult compress_zstd(const void* data, size_t size, int level);

    DecompressionResult decompress_lz4(const void* data, size_t size, size_t original_size);
    DecompressionResult decompress_zstd(const void* data, size_t size, size_t original_size);
};

/**
 * @brief Streaming compressor for large data
 */
class StreamingCompressor {
public:
    explicit StreamingCompressor(const CompressionConfig& config);
    ~StreamingCompressor();

    /**
     * @brief Feed data to compressor
     * @param data Input data chunk
     * @param size Size in bytes
     * @return Compressed output (may be empty until enough data)
     */
    std::vector<uint8_t> feed(const void* data, size_t size);

    /**
     * @brief Finish compression and get remaining data
     * @return Final compressed output
     */
    std::vector<uint8_t> finish();

    /**
     * @brief Reset for new compression
     */
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Streaming decompressor for large data
 */
class StreamingDecompressor {
public:
    StreamingDecompressor();
    ~StreamingDecompressor();

    /**
     * @brief Feed compressed data
     * @param data Compressed data chunk
     * @param size Size in bytes
     * @return Decompressed output
     */
    std::vector<uint8_t> feed(const void* data, size_t size);

    /**
     * @brief Finish decompression
     * @return Final decompressed output
     */
    std::vector<uint8_t> finish();

    /**
     * @brief Reset for new decompression
     */
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace snapllm
