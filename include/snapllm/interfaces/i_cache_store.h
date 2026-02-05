/**
 * @file i_cache_store.h
 * @brief Interface for KV Cache Storage
 *
 * Defines the contract for persistent KV cache storage backends.
 * Supports tiered storage with different backends for hot/warm/cold data.
 *
 * Design Principles:
 * - Abstract storage backend details
 * - Support for different serialization formats
 * - Atomic read/write operations
 * - Integrity verification
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <chrono>
#include <memory>

namespace snapllm {

/**
 * @brief Cache entry metadata
 */
struct CacheEntryInfo {
    std::string cache_id;
    size_t size_bytes = 0;
    uint32_t checksum = 0;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_accessed;
    uint64_t access_count = 0;

    // KV cache specific
    uint32_t num_layers = 0;
    uint32_t num_heads = 0;
    uint32_t head_dim = 0;
    uint32_t sequence_length = 0;
    std::string model_id;
};

/**
 * @brief Write options for cache store
 */
struct CacheWriteOptions {
    bool compress = false;          ///< Enable compression (LZ4/ZSTD)
    bool verify_checksum = true;    ///< Verify write integrity
    bool sync_write = false;        ///< Force sync to disk
    int compression_level = 1;      ///< Compression level (1-9)
};

/**
 * @brief Read options for cache store
 */
struct CacheReadOptions {
    bool verify_checksum = true;    ///< Verify read integrity
    bool decompress = true;         ///< Auto-decompress if compressed
    size_t prefetch_size = 0;       ///< Bytes to prefetch ahead
};

/**
 * @brief Write result
 */
struct CacheWriteResult {
    bool success = false;
    size_t bytes_written = 0;
    uint32_t checksum = 0;
    std::string error_message;
    double write_time_ms = 0.0;
};

/**
 * @brief Read result
 */
struct CacheReadResult {
    bool success = false;
    std::vector<uint8_t> data;
    size_t bytes_read = 0;
    uint32_t checksum = 0;
    std::string error_message;
    double read_time_ms = 0.0;
    bool was_compressed = false;
};

/**
 * @brief Cache store statistics
 */
struct CacheStoreStats {
    size_t total_entries = 0;
    size_t total_size_bytes = 0;
    size_t capacity_bytes = 0;

    uint64_t reads = 0;
    uint64_t writes = 0;
    uint64_t deletes = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;

    double avg_read_time_ms = 0.0;
    double avg_write_time_ms = 0.0;

    double hit_rate() const {
        uint64_t total = hits + misses;
        return total > 0 ? (double)hits / total : 0.0;
    }
};

/**
 * @brief Interface for KV cache persistent storage
 *
 * Contract:
 * - write() is atomic: either fully succeeds or has no effect
 * - read() returns complete data or failure (no partial reads)
 * - exists() and get_info() are always consistent
 * - delete() removes all traces of cache entry
 *
 * Thread Safety:
 * - All methods are thread-safe
 * - Concurrent writes to same cache_id are serialized
 * - Read during write returns consistent (possibly stale) data
 */
class ICacheStore {
public:
    virtual ~ICacheStore() = default;

    //=========================================================================
    // Core Operations
    //=========================================================================

    /**
     * @brief Write cache data to store
     * @param cache_id Unique cache identifier
     * @param data Data to write
     * @param size Size in bytes
     * @param info Metadata about the cache entry
     * @param options Write options
     * @return Write result
     */
    virtual CacheWriteResult write(
        const std::string& cache_id,
        const void* data,
        size_t size,
        const CacheEntryInfo& info,
        const CacheWriteOptions& options = CacheWriteOptions{}
    ) = 0;

    /**
     * @brief Read cache data from store
     * @param cache_id Cache identifier
     * @param options Read options
     * @return Read result with data
     */
    virtual CacheReadResult read(
        const std::string& cache_id,
        const CacheReadOptions& options = CacheReadOptions{}
    ) = 0;

    /**
     * @brief Read cache data into pre-allocated buffer
     * @param cache_id Cache identifier
     * @param buffer Pre-allocated buffer
     * @param buffer_size Buffer size
     * @param options Read options
     * @return Read result (data field empty, check bytes_read)
     */
    virtual CacheReadResult read_into(
        const std::string& cache_id,
        void* buffer,
        size_t buffer_size,
        const CacheReadOptions& options = CacheReadOptions{}
    ) = 0;

    /**
     * @brief Delete cache entry
     * @param cache_id Cache identifier
     * @return true if deleted, false if not found
     */
    virtual bool remove(const std::string& cache_id) = 0;

    /**
     * @brief Check if cache exists
     * @param cache_id Cache identifier
     * @return true if exists
     */
    virtual bool exists(const std::string& cache_id) const = 0;

    //=========================================================================
    // Metadata Operations
    //=========================================================================

    /**
     * @brief Get cache entry info
     * @param cache_id Cache identifier
     * @return Entry info if exists
     */
    virtual std::optional<CacheEntryInfo> get_info(const std::string& cache_id) const = 0;

    /**
     * @brief Update access metadata (for LRU tracking)
     * @param cache_id Cache identifier
     */
    virtual void touch(const std::string& cache_id) = 0;

    /**
     * @brief List all cache entries
     * @return Vector of cache IDs
     */
    virtual std::vector<std::string> list() const = 0;

    /**
     * @brief List cache entries matching prefix
     * @param prefix ID prefix to match
     * @return Vector of matching cache IDs
     */
    virtual std::vector<std::string> list_by_prefix(const std::string& prefix) const = 0;

    /**
     * @brief List cache entries for a model
     * @param model_id Model identifier
     * @return Vector of cache IDs for the model
     */
    virtual std::vector<std::string> list_by_model(const std::string& model_id) const = 0;

    //=========================================================================
    // Maintenance Operations
    //=========================================================================

    /**
     * @brief Compact storage (defragment, reclaim space)
     * @return Bytes reclaimed
     */
    virtual size_t compact() = 0;

    /**
     * @brief Verify integrity of all entries
     * @return Vector of corrupted cache IDs
     */
    virtual std::vector<std::string> verify_integrity() = 0;

    /**
     * @brief Verify integrity of specific entry
     * @param cache_id Cache identifier
     * @return true if valid
     */
    virtual bool verify(const std::string& cache_id) = 0;

    /**
     * @brief Clear all entries
     * @return Number of entries cleared
     */
    virtual size_t clear() = 0;

    //=========================================================================
    // Capacity Management
    //=========================================================================

    /**
     * @brief Get store capacity
     * @return Capacity in bytes
     */
    virtual size_t capacity() const = 0;

    /**
     * @brief Get used space
     * @return Used bytes
     */
    virtual size_t used() const = 0;

    /**
     * @brief Get available space
     * @return Available bytes
     */
    virtual size_t available() const {
        return capacity() - used();
    }

    /**
     * @brief Set capacity limit
     * @param bytes New capacity limit
     */
    virtual void set_capacity(size_t bytes) = 0;

    //=========================================================================
    // Statistics
    //=========================================================================

    /**
     * @brief Get store statistics
     * @return Statistics
     */
    virtual CacheStoreStats get_stats() const = 0;

    /**
     * @brief Reset statistics counters
     */
    virtual void reset_stats() = 0;

    //=========================================================================
    // Persistence
    //=========================================================================

    /**
     * @brief Sync all pending writes to disk
     */
    virtual void sync() = 0;

    /**
     * @brief Get store path
     * @return Path to store location
     */
    virtual std::string get_path() const = 0;
};

/**
 * @brief Factory function signature for cache stores
 */
using CacheStoreFactory = std::function<std::unique_ptr<ICacheStore>(
    const std::string& path,
    size_t capacity
)>;

} // namespace snapllm
