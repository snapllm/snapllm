/**
 * @file kv_cache.h
 * @brief KV Cache Data Structures for vPID L2
 *
 * Defines the core data structures for Key-Value cache storage
 * that enables O(1) context query after ingestion.
 *
 * KV Cache Structure:
 * - Per-layer K and V tensors
 * - Supports different dtypes (fp32, fp16, bf16, int8)
 * - Persistent storage format (.kvc files)
 * - Memory-efficient views for query processing
 *
 * File Format:
 * [KVCacheFileHeader - 256 bytes]
 * [Layer 0 Keys   - num_heads * seq_len * head_dim * dtype_size]
 * [Layer 0 Values - num_heads * seq_len * head_dim * dtype_size]
 * [Layer 1 Keys   - ...]
 * ...
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>
#include <algorithm>

namespace snapllm {

//=============================================================================
// Data Type Definitions
//=============================================================================

/**
 * @brief Supported data types for KV cache
 */
enum class KVDataType : uint32_t {
    FP32 = 0,   ///< 32-bit float (4 bytes)
    FP16 = 1,   ///< 16-bit float (2 bytes)
    BF16 = 2,   ///< Brain float 16 (2 bytes)
    INT8 = 3,   ///< 8-bit quantized (1 byte)
    INT4 = 4    ///< 4-bit quantized (0.5 bytes, packed)
};

/**
 * @brief Get size in bytes for a data type
 */
inline size_t kv_dtype_size(KVDataType dtype) {
    switch (dtype) {
        case KVDataType::FP32: return 4;
        case KVDataType::FP16: return 2;
        case KVDataType::BF16: return 2;
        case KVDataType::INT8: return 1;
        case KVDataType::INT4: return 1;  // 2 values packed per byte
        default: return 4;
    }
}

/**
 * @brief Get string name for data type
 */
inline const char* kv_dtype_name(KVDataType dtype) {
    switch (dtype) {
        case KVDataType::FP32: return "fp32";
        case KVDataType::FP16: return "fp16";
        case KVDataType::BF16: return "bf16";
        case KVDataType::INT8: return "int8";
        case KVDataType::INT4: return "int4";
        default: return "unknown";
    }
}

//=============================================================================
// KV Cache Shape and Configuration
//=============================================================================

/**
 * @brief KV cache shape descriptor
 *
 * Defines the dimensions of a KV cache for a specific model.
 */
struct KVCacheShape {
    uint32_t num_layers = 0;      ///< Number of transformer layers
    uint32_t num_heads = 0;       ///< Number of attention heads
    uint32_t head_dim = 0;        ///< Dimension per head
    uint32_t sequence_length = 0; ///< Number of tokens
    KVDataType dtype = KVDataType::FP16;

    /**
     * @brief Calculate size in bytes for one layer (K or V)
     */
    size_t layer_tensor_size() const {
        size_t elem_size = kv_dtype_size(dtype);
        if (dtype == KVDataType::INT4) {
            // INT4 packs 2 values per byte
            return (num_heads * sequence_length * head_dim + 1) / 2;
        }
        return num_heads * sequence_length * head_dim * elem_size;
    }

    /**
     * @brief Calculate total size for all K and V tensors
     */
    size_t total_size() const {
        // 2 tensors (K, V) per layer
        return 2 * num_layers * layer_tensor_size();
    }

    /**
     * @brief Get offset to a specific layer's K tensor
     */
    size_t layer_k_offset(uint32_t layer) const {
        return 2 * layer * layer_tensor_size();
    }

    /**
     * @brief Get offset to a specific layer's V tensor
     */
    size_t layer_v_offset(uint32_t layer) const {
        return (2 * layer + 1) * layer_tensor_size();
    }

    bool is_valid() const {
        return num_layers > 0 && num_heads > 0 && head_dim > 0 && sequence_length > 0;
    }
};

//=============================================================================
// Per-Layer KV Tensors
//=============================================================================

/**
 * @brief Single layer's K and V tensors
 *
 * Stores the pre-computed Key and Value tensors for one transformer layer.
 * Shape: [num_heads, sequence_length, head_dim]
 */
struct KVLayerCache {
    std::vector<uint8_t> keys;    ///< Key tensor data
    std::vector<uint8_t> values;  ///< Value tensor data

    // Tensor views (pointers into keys/values)
    void* keys_ptr = nullptr;
    void* values_ptr = nullptr;

    KVLayerCache() = default;

    /**
     * @brief Allocate storage for keys and values
     */
    void allocate(size_t tensor_size) {
        keys.resize(tensor_size);
        values.resize(tensor_size);
        keys_ptr = keys.data();
        values_ptr = values.data();
    }

    /**
     * @brief Get typed pointer to keys
     */
    template<typename T>
    T* keys_as() { return reinterpret_cast<T*>(keys_ptr ? keys_ptr : keys.data()); }

    template<typename T>
    const T* keys_as() const { return reinterpret_cast<const T*>(keys_ptr ? keys_ptr : keys.data()); }

    /**
     * @brief Get typed pointer to values
     */
    template<typename T>
    T* values_as() { return reinterpret_cast<T*>(values_ptr ? values_ptr : values.data()); }

    template<typename T>
    const T* values_as() const { return reinterpret_cast<const T*>(values_ptr ? values_ptr : values.data()); }

    size_t size_bytes() const {
        return keys.size() + values.size();
    }
};

//=============================================================================
// Complete KV Cache
//=============================================================================

/**
 * @brief Complete KV cache for a context
 *
 * Contains all layer KV caches for a pre-processed context.
 * This is the core data structure that enables O(1) query access.
 */
struct KVCache {
    std::string context_id;       ///< Unique context identifier
    std::string model_id;         ///< Model this cache is for
    KVCacheShape shape;           ///< Shape descriptor

    std::vector<KVLayerCache> layers;  ///< Per-layer K/V data

    // Metadata
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_accessed;
    uint64_t access_count = 0;
    uint32_t checksum = 0;

    KVCache() : created_at(std::chrono::system_clock::now()),
                last_accessed(std::chrono::system_clock::now()) {}

    /**
     * @brief Allocate storage for all layers
     */
    void allocate() {
        if (!shape.is_valid()) return;

        layers.resize(shape.num_layers);
        size_t tensor_size = shape.layer_tensor_size();

        for (auto& layer : layers) {
            layer.allocate(tensor_size);
        }
    }

    /**
     * @brief Total memory footprint in bytes
     */
    size_t memory_bytes() const {
        size_t total = 0;
        for (const auto& layer : layers) {
            total += layer.size_bytes();
        }
        return total;
    }

    /**
     * @brief Check if cache is allocated and valid
     */
    bool is_valid() const {
        return shape.is_valid() &&
               layers.size() == shape.num_layers &&
               !context_id.empty();
    }

    /**
     * @brief Record an access (for LRU tracking)
     */
    void touch() {
        last_accessed = std::chrono::system_clock::now();
        access_count++;
    }
};

//=============================================================================
// KV Cache View (Non-owning reference)
//=============================================================================

/**
 * @brief Non-owning view into a KV cache
 *
 * Used for passing KV cache data to inference without copying.
 * The underlying KVCache must outlive the view.
 */
struct KVCacheView {
    const KVCache* cache = nullptr;

    KVCacheView() = default;
    explicit KVCacheView(const KVCache& c) : cache(&c) {}
    explicit KVCacheView(const KVCache* c) : cache(c) {}

    bool is_valid() const { return cache && cache->is_valid(); }

    const KVCacheShape& shape() const { return cache->shape; }
    uint32_t num_layers() const { return cache->shape.num_layers; }
    uint32_t sequence_length() const { return cache->shape.sequence_length; }

    /**
     * @brief Get keys for a layer
     */
    template<typename T>
    const T* layer_keys(uint32_t layer) const {
        if (!cache || layer >= cache->layers.size()) return nullptr;
        return cache->layers[layer].template keys_as<T>();
    }

    /**
     * @brief Get values for a layer
     */
    template<typename T>
    const T* layer_values(uint32_t layer) const {
        if (!cache || layer >= cache->layers.size()) return nullptr;
        return cache->layers[layer].template values_as<T>();
    }
};

//=============================================================================
// KV Cache File Format
//=============================================================================

/**
 * @brief File header for .kvc files
 *
 * Fixed 256-byte header for KV cache persistence.
 */
struct KVCacheFileHeader {
    char magic[4] = {'S', 'K', 'V', 'C'};  ///< "SKVC" - SnapLLM KV Cache
    uint32_t version = 1;
    uint32_t flags = 0;                     ///< Compression, quantization flags

    // Context metadata
    char context_id[64] = {0};
    char model_id[64] = {0};
    uint64_t created_timestamp = 0;

    // KV shape
    uint32_t num_layers = 0;
    uint32_t num_heads = 0;
    uint32_t head_dim = 0;
    uint32_t sequence_length = 0;
    uint32_t dtype = 0;                     ///< KVDataType as uint32

    // Integrity
    uint64_t data_size = 0;
    uint32_t header_checksum = 0;
    uint32_t data_checksum = 0;

    // Reserved for future use
    uint8_t reserved[64] = {0};

    // Flags
    static constexpr uint32_t FLAG_COMPRESSED = 0x01;
    static constexpr uint32_t FLAG_QUANTIZED = 0x02;

    bool is_compressed() const { return flags & FLAG_COMPRESSED; }
    bool is_quantized() const { return flags & FLAG_QUANTIZED; }

    /**
     * @brief Validate header magic
     */
    bool is_valid() const {
        return magic[0] == 'S' && magic[1] == 'K' &&
               magic[2] == 'V' && magic[3] == 'C';
    }

    /**
     * @brief Get shape from header
     */
    KVCacheShape get_shape() const {
        KVCacheShape shape;
        shape.num_layers = num_layers;
        shape.num_heads = num_heads;
        shape.head_dim = head_dim;
        shape.sequence_length = sequence_length;
        shape.dtype = static_cast<KVDataType>(dtype);
        return shape;
    }

    /**
     * @brief Set shape in header
     */
    void set_shape(const KVCacheShape& shape) {
        num_layers = shape.num_layers;
        num_heads = shape.num_heads;
        head_dim = shape.head_dim;
        sequence_length = shape.sequence_length;
        dtype = static_cast<uint32_t>(shape.dtype);
    }

    /**
     * @brief Set context ID (safely truncates if too long)
     */
    void set_context_id(const std::string& id) {
        size_t len = (std::min)(id.size(), sizeof(context_id) - 1);
        std::memcpy(context_id, id.c_str(), len);
        context_id[len] = '\0';
    }

    /**
     * @brief Set model ID (safely truncates if too long)
     */
    void set_model_id(const std::string& id) {
        size_t len = (std::min)(id.size(), sizeof(model_id) - 1);
        std::memcpy(model_id, id.c_str(), len);
        model_id[len] = '\0';
    }

    std::string get_context_id() const { return std::string(context_id); }
    std::string get_model_id() const { return std::string(model_id); }
};

static_assert(sizeof(KVCacheFileHeader) == 256, "KVCacheFileHeader must be 256 bytes");

//=============================================================================
// KV Cache Configuration
//=============================================================================

/**
 * @brief Configuration for KV cache computation
 */
struct KVCacheConfig {
    KVDataType dtype = KVDataType::FP16;  ///< Storage data type
    bool compress_on_store = false;        ///< Compress when saving to cold tier
    int compression_level = 1;             ///< Compression level (1-9)

    // Quantization options
    bool quantize = false;
    KVDataType quantize_dtype = KVDataType::INT8;

    // Chunking for large contexts
    uint32_t max_chunk_tokens = 8192;     ///< Max tokens per chunk
    uint32_t chunk_overlap = 512;          ///< Overlap between chunks
};

//=============================================================================
// Utility Functions
//=============================================================================

/**
 * @brief Generate a unique context ID
 */
std::string generate_context_id();

/**
 * @brief Compute CRC32 checksum
 */
uint32_t compute_checksum(const void* data, size_t size);

/**
 * @brief Estimate KV cache memory for given parameters
 */
inline size_t estimate_kv_cache_size(
    uint32_t num_layers,
    uint32_t num_heads,
    uint32_t head_dim,
    uint32_t sequence_length,
    KVDataType dtype = KVDataType::FP16
) {
    KVCacheShape shape;
    shape.num_layers = num_layers;
    shape.num_heads = num_heads;
    shape.head_dim = head_dim;
    shape.sequence_length = sequence_length;
    shape.dtype = dtype;
    return shape.total_size();
}

} // namespace snapllm
