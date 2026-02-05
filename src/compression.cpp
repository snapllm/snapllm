/**
 * @file compression.cpp
 * @brief Compression Implementation for vPID L2
 *
 * Implements LZ4 and ZSTD compression with fallback for unavailable libraries.
 */

#include "snapllm/compression.h"
#include <iostream>
#include <cstring>
#include <chrono>

// Conditional LZ4 support
#ifdef SNAPLLM_HAS_LZ4
#include <lz4.h>
#include <lz4hc.h>
#define LZ4_AVAILABLE 1
#else
#define LZ4_AVAILABLE 0
#endif

// Conditional ZSTD support
#ifdef SNAPLLM_HAS_ZSTD
#include <zstd.h>
#define ZSTD_AVAILABLE 1
#else
#define ZSTD_AVAILABLE 0
#endif

namespace snapllm {

//=============================================================================
// Compressor Implementation
//=============================================================================

Compressor::Compressor() {
    // Log available compression libraries
    std::cout << "[Compressor] Initialized. Available: ";
#if LZ4_AVAILABLE
    std::cout << "LZ4 ";
#endif
#if ZSTD_AVAILABLE
    std::cout << "ZSTD ";
#endif
#if !LZ4_AVAILABLE && !ZSTD_AVAILABLE
    std::cout << "(none - fallback mode)";
#endif
    std::cout << std::endl;
}

Compressor::~Compressor() = default;

//=============================================================================
// Compression
//=============================================================================

CompressionResult Compressor::compress(
    const std::vector<uint8_t>& data,
    const CompressionConfig& config
) {
    return compress(data.data(), data.size(), config);
}

CompressionResult Compressor::compress(
    const void* data,
    size_t size,
    const CompressionConfig& config
) {
    if (!data || size == 0) {
        return CompressionResult::fail("Empty input data");
    }

    auto start = std::chrono::high_resolution_clock::now();

    CompressionResult result;

    switch (config.type) {
        case CompressionType::None: {
            // No compression - just copy with header
            result.data.resize(sizeof(CompressedHeader) + size);

            CompressedHeader header;
            header.set_type(CompressionType::None);
            header.original_size = size;

            std::memcpy(result.data.data(), &header, sizeof(header));
            std::memcpy(result.data.data() + sizeof(header), data, size);

            result.success = true;
            result.original_size = size;
            result.compressed_size = result.data.size();
            break;
        }

        case CompressionType::LZ4:
            result = compress_lz4(data, size, config.level > 0 ? config.level : 1);
            break;

        case CompressionType::LZ4_HC:
            result = compress_lz4_hc(data, size, config.level > 0 ? config.level : 9);
            break;

        case CompressionType::ZSTD:
        case CompressionType::ZSTD_FAST:
            result = compress_zstd(data, size, config.level > 0 ? config.level : 3);
            break;

        default:
            return CompressionResult::fail("Unknown compression type");
    }

    auto end = std::chrono::high_resolution_clock::now();
    result.time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    return result;
}

CompressionResult Compressor::compress_lz4(const void* data, size_t size, int level) {
#if LZ4_AVAILABLE
    // Calculate maximum compressed size
    int max_dst_size = LZ4_compressBound(static_cast<int>(size));
    if (max_dst_size <= 0) {
        return CompressionResult::fail("LZ4: Input too large");
    }

    // Allocate output buffer with header
    std::vector<uint8_t> output(sizeof(CompressedHeader) + max_dst_size);

    // Compress
    int compressed_size = LZ4_compress_default(
        static_cast<const char*>(data),
        reinterpret_cast<char*>(output.data() + sizeof(CompressedHeader)),
        static_cast<int>(size),
        max_dst_size
    );

    if (compressed_size <= 0) {
        return CompressionResult::fail("LZ4: Compression failed");
    }

    // Write header
    CompressedHeader header;
    header.set_type(CompressionType::LZ4);
    header.original_size = size;
    std::memcpy(output.data(), &header, sizeof(header));

    // Resize to actual size
    output.resize(sizeof(CompressedHeader) + compressed_size);

    return CompressionResult::ok(std::move(output), size, 0.0);
#else
    // Fallback: return uncompressed with warning
    std::cerr << "[Compressor] Warning: LZ4 not available, returning uncompressed" << std::endl;

    std::vector<uint8_t> output(sizeof(CompressedHeader) + size);

    CompressedHeader header;
    header.set_type(CompressionType::None);
    header.original_size = size;

    std::memcpy(output.data(), &header, sizeof(header));
    std::memcpy(output.data() + sizeof(header), data, size);

    return CompressionResult::ok(std::move(output), size, 0.0);
#endif
}

CompressionResult Compressor::compress_lz4_hc(const void* data, size_t size, int level) {
#if LZ4_AVAILABLE
    // Calculate maximum compressed size
    int max_dst_size = LZ4_compressBound(static_cast<int>(size));
    if (max_dst_size <= 0) {
        return CompressionResult::fail("LZ4_HC: Input too large");
    }

    // Allocate output buffer with header
    std::vector<uint8_t> output(sizeof(CompressedHeader) + max_dst_size);

    // Compress with high compression
    int compressed_size = LZ4_compress_HC(
        static_cast<const char*>(data),
        reinterpret_cast<char*>(output.data() + sizeof(CompressedHeader)),
        static_cast<int>(size),
        max_dst_size,
        level
    );

    if (compressed_size <= 0) {
        return CompressionResult::fail("LZ4_HC: Compression failed");
    }

    // Write header
    CompressedHeader header;
    header.set_type(CompressionType::LZ4_HC);
    header.original_size = size;
    std::memcpy(output.data(), &header, sizeof(header));

    // Resize to actual size
    output.resize(sizeof(CompressedHeader) + compressed_size);

    return CompressionResult::ok(std::move(output), size, 0.0);
#else
    // Fallback to regular LZ4 or uncompressed
    return compress_lz4(data, size, level);
#endif
}

CompressionResult Compressor::compress_zstd(const void* data, size_t size, int level) {
#if ZSTD_AVAILABLE
    // Calculate maximum compressed size
    size_t max_dst_size = ZSTD_compressBound(size);

    // Allocate output buffer with header
    std::vector<uint8_t> output(sizeof(CompressedHeader) + max_dst_size);

    // Compress
    size_t compressed_size = ZSTD_compress(
        output.data() + sizeof(CompressedHeader),
        max_dst_size,
        data,
        size,
        level
    );

    if (ZSTD_isError(compressed_size)) {
        return CompressionResult::fail(std::string("ZSTD: ") + ZSTD_getErrorName(compressed_size));
    }

    // Write header
    CompressedHeader header;
    header.set_type(CompressionType::ZSTD);
    header.original_size = size;
    std::memcpy(output.data(), &header, sizeof(header));

    // Resize to actual size
    output.resize(sizeof(CompressedHeader) + compressed_size);

    return CompressionResult::ok(std::move(output), size, 0.0);
#else
    // Fallback: try LZ4, or return uncompressed
    std::cerr << "[Compressor] Warning: ZSTD not available, falling back to LZ4" << std::endl;
    return compress_lz4(data, size, 1);
#endif
}

//=============================================================================
// Decompression
//=============================================================================

DecompressionResult Compressor::decompress(const std::vector<uint8_t>& data) {
    return decompress(data.data(), data.size());
}

DecompressionResult Compressor::decompress(const void* data, size_t size) {
    if (!data || size < sizeof(CompressedHeader)) {
        return DecompressionResult::fail("Input too small for header");
    }

    auto start = std::chrono::high_resolution_clock::now();

    // Read header
    auto header_opt = read_header(data, size);
    if (!header_opt) {
        return DecompressionResult::fail("Invalid compression header");
    }

    const CompressedHeader& header = *header_opt;
    const uint8_t* compressed_data = static_cast<const uint8_t*>(data) + sizeof(CompressedHeader);
    size_t compressed_size = size - sizeof(CompressedHeader);

    DecompressionResult result;

    switch (header.get_type()) {
        case CompressionType::None: {
            // No compression - just copy
            result.data.resize(header.original_size);
            std::memcpy(result.data.data(), compressed_data, header.original_size);
            result.success = true;
            result.decompressed_size = header.original_size;
            break;
        }

        case CompressionType::LZ4:
        case CompressionType::LZ4_HC:
            result = decompress_lz4(compressed_data, compressed_size, header.original_size);
            break;

        case CompressionType::ZSTD:
        case CompressionType::ZSTD_FAST:
            result = decompress_zstd(compressed_data, compressed_size, header.original_size);
            break;

        default:
            return DecompressionResult::fail("Unknown compression type in header");
    }

    auto end = std::chrono::high_resolution_clock::now();
    result.time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    return result;
}

DecompressionResult Compressor::decompress_lz4(const void* data, size_t size, size_t original_size) {
#if LZ4_AVAILABLE
    std::vector<uint8_t> output(original_size);

    int decompressed_size = LZ4_decompress_safe(
        static_cast<const char*>(data),
        reinterpret_cast<char*>(output.data()),
        static_cast<int>(size),
        static_cast<int>(original_size)
    );

    if (decompressed_size < 0) {
        return DecompressionResult::fail("LZ4: Decompression failed");
    }

    if (static_cast<size_t>(decompressed_size) != original_size) {
        return DecompressionResult::fail("LZ4: Size mismatch after decompression");
    }

    return DecompressionResult::ok(std::move(output), 0.0);
#else
    return DecompressionResult::fail("LZ4 not available");
#endif
}

DecompressionResult Compressor::decompress_zstd(const void* data, size_t size, size_t original_size) {
#if ZSTD_AVAILABLE
    std::vector<uint8_t> output(original_size);

    size_t decompressed_size = ZSTD_decompress(
        output.data(),
        original_size,
        data,
        size
    );

    if (ZSTD_isError(decompressed_size)) {
        return DecompressionResult::fail(std::string("ZSTD: ") + ZSTD_getErrorName(decompressed_size));
    }

    if (decompressed_size != original_size) {
        return DecompressionResult::fail("ZSTD: Size mismatch after decompression");
    }

    return DecompressionResult::ok(std::move(output), 0.0);
#else
    return DecompressionResult::fail("ZSTD not available");
#endif
}

//=============================================================================
// Utilities
//=============================================================================

size_t Compressor::max_compressed_size(size_t input_size, CompressionType type) {
    switch (type) {
        case CompressionType::None:
            return sizeof(CompressedHeader) + input_size;

        case CompressionType::LZ4:
        case CompressionType::LZ4_HC:
#if LZ4_AVAILABLE
            return sizeof(CompressedHeader) + LZ4_compressBound(static_cast<int>(input_size));
#else
            return sizeof(CompressedHeader) + input_size;
#endif

        case CompressionType::ZSTD:
        case CompressionType::ZSTD_FAST:
#if ZSTD_AVAILABLE
            return sizeof(CompressedHeader) + ZSTD_compressBound(input_size);
#else
            return sizeof(CompressedHeader) + input_size;
#endif

        default:
            return sizeof(CompressedHeader) + input_size;
    }
}

bool Compressor::has_header(const void* data, size_t size) {
    if (size < sizeof(CompressedHeader)) {
        return false;
    }

    const CompressedHeader* header = static_cast<const CompressedHeader*>(data);
    return header->is_valid();
}

std::optional<CompressedHeader> Compressor::read_header(const void* data, size_t size) {
    if (size < sizeof(CompressedHeader)) {
        return std::nullopt;
    }

    CompressedHeader header;
    std::memcpy(&header, data, sizeof(header));

    if (!header.is_valid()) {
        return std::nullopt;
    }

    return header;
}

bool Compressor::is_available(CompressionType type) {
    switch (type) {
        case CompressionType::None:
            return true;

        case CompressionType::LZ4:
        case CompressionType::LZ4_HC:
#if LZ4_AVAILABLE
            return true;
#else
            return false;
#endif

        case CompressionType::ZSTD:
        case CompressionType::ZSTD_FAST:
#if ZSTD_AVAILABLE
            return true;
#else
            return false;
#endif

        default:
            return false;
    }
}

//=============================================================================
// StreamingCompressor Implementation
//=============================================================================

struct StreamingCompressor::Impl {
    CompressionConfig config;
    std::vector<uint8_t> buffer;
    bool header_written = false;

#if ZSTD_AVAILABLE
    ZSTD_CStream* zstd_stream = nullptr;
#endif

    Impl(const CompressionConfig& cfg) : config(cfg) {
#if ZSTD_AVAILABLE
        if (config.type == CompressionType::ZSTD || config.type == CompressionType::ZSTD_FAST) {
            zstd_stream = ZSTD_createCStream();
            ZSTD_initCStream(zstd_stream, config.level);
        }
#endif
    }

    ~Impl() {
#if ZSTD_AVAILABLE
        if (zstd_stream) {
            ZSTD_freeCStream(zstd_stream);
        }
#endif
    }
};

StreamingCompressor::StreamingCompressor(const CompressionConfig& config)
    : impl_(std::make_unique<Impl>(config))
{
}

StreamingCompressor::~StreamingCompressor() = default;

std::vector<uint8_t> StreamingCompressor::feed(const void* data, size_t size) {
    std::vector<uint8_t> output;

    // For simplicity, buffer all data and compress on finish
    // A full implementation would use streaming APIs
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    impl_->buffer.insert(impl_->buffer.end(), bytes, bytes + size);

    return output;  // Empty until finish()
}

std::vector<uint8_t> StreamingCompressor::finish() {
    Compressor compressor;
    auto result = compressor.compress(impl_->buffer, impl_->config);

    impl_->buffer.clear();

    if (result.success) {
        return std::move(result.data);
    }

    return {};
}

void StreamingCompressor::reset() {
    impl_->buffer.clear();
    impl_->header_written = false;
}

//=============================================================================
// StreamingDecompressor Implementation
//=============================================================================

struct StreamingDecompressor::Impl {
    std::vector<uint8_t> buffer;
    bool header_read = false;
    CompressedHeader header;

#if ZSTD_AVAILABLE
    ZSTD_DStream* zstd_stream = nullptr;
#endif

    Impl() {
#if ZSTD_AVAILABLE
        zstd_stream = ZSTD_createDStream();
        ZSTD_initDStream(zstd_stream);
#endif
    }

    ~Impl() {
#if ZSTD_AVAILABLE
        if (zstd_stream) {
            ZSTD_freeDStream(zstd_stream);
        }
#endif
    }
};

StreamingDecompressor::StreamingDecompressor()
    : impl_(std::make_unique<Impl>())
{
}

StreamingDecompressor::~StreamingDecompressor() = default;

std::vector<uint8_t> StreamingDecompressor::feed(const void* data, size_t size) {
    std::vector<uint8_t> output;

    // Buffer data
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    impl_->buffer.insert(impl_->buffer.end(), bytes, bytes + size);

    return output;  // Empty until finish()
}

std::vector<uint8_t> StreamingDecompressor::finish() {
    Compressor compressor;
    auto result = compressor.decompress(impl_->buffer);

    impl_->buffer.clear();

    if (result.success) {
        return std::move(result.data);
    }

    return {};
}

void StreamingDecompressor::reset() {
    impl_->buffer.clear();
    impl_->header_read = false;
}

} // namespace snapllm
