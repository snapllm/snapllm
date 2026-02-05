/**
 * @file file_cache_store.h
 * @brief File-based KV Cache Store Implementation
 *
 * Implements ICacheStore using filesystem storage.
 * Designed for the "cold" tier of vPID L2 context storage.
 *
 * Features:
 * - Atomic writes (write-to-temp, then rename)
 * - Integrity verification via checksums
 * - Metadata caching for fast lookups
 * - Optional compression support
 */

#pragma once

#include "interfaces/i_cache_store.h"
#include <filesystem>
#include <unordered_map>
#include <shared_mutex>
#include <fstream>

namespace snapllm {

namespace fs = std::filesystem;

/**
 * @brief File-based implementation of ICacheStore
 *
 * Stores KV caches as individual .kvc files in a directory.
 * Each cache entry consists of:
 * - <cache_id>.kvc - Binary KV cache data
 * - <cache_id>.meta - JSON metadata file
 *
 * Thread Safety:
 * - All public methods are thread-safe
 * - Uses read-write locks for concurrent access
 * - Write operations are atomic (write-rename pattern)
 */
class FileCacheStore : public ICacheStore {
public:
    /**
     * @brief Construct file cache store
     * @param path Directory path for cache storage
     * @param capacity Maximum storage capacity in bytes (0 = unlimited)
     */
    explicit FileCacheStore(const fs::path& path, size_t capacity = 0);

    ~FileCacheStore() override;

    // Prevent copying
    FileCacheStore(const FileCacheStore&) = delete;
    FileCacheStore& operator=(const FileCacheStore&) = delete;

    //=========================================================================
    // Core Operations
    //=========================================================================

    CacheWriteResult write(
        const std::string& cache_id,
        const void* data,
        size_t size,
        const CacheEntryInfo& info,
        const CacheWriteOptions& options = CacheWriteOptions{}
    ) override;

    CacheReadResult read(
        const std::string& cache_id,
        const CacheReadOptions& options = CacheReadOptions{}
    ) override;

    CacheReadResult read_into(
        const std::string& cache_id,
        void* buffer,
        size_t buffer_size,
        const CacheReadOptions& options = CacheReadOptions{}
    ) override;

    bool remove(const std::string& cache_id) override;

    bool exists(const std::string& cache_id) const override;

    //=========================================================================
    // Metadata Operations
    //=========================================================================

    std::optional<CacheEntryInfo> get_info(const std::string& cache_id) const override;

    void touch(const std::string& cache_id) override;

    std::vector<std::string> list() const override;

    std::vector<std::string> list_by_prefix(const std::string& prefix) const override;

    std::vector<std::string> list_by_model(const std::string& model_id) const override;

    //=========================================================================
    // Maintenance Operations
    //=========================================================================

    size_t compact() override;

    std::vector<std::string> verify_integrity() override;

    bool verify(const std::string& cache_id) override;

    size_t clear() override;

    //=========================================================================
    // Capacity Management
    //=========================================================================

    size_t capacity() const override;

    size_t used() const override;

    void set_capacity(size_t bytes) override;

    //=========================================================================
    // Statistics
    //=========================================================================

    CacheStoreStats get_stats() const override;

    void reset_stats() override;

    //=========================================================================
    // Persistence
    //=========================================================================

    void sync() override;

    std::string get_path() const override;

    //=========================================================================
    // Additional Methods
    //=========================================================================

    /**
     * @brief Scan directory and rebuild metadata cache
     */
    void rebuild_index();

    /**
     * @brief Get file path for a cache entry
     */
    fs::path get_cache_file_path(const std::string& cache_id) const;

    /**
     * @brief Get metadata file path for a cache entry
     */
    fs::path get_meta_file_path(const std::string& cache_id) const;

private:
    fs::path store_path_;
    size_t capacity_bytes_;
    mutable size_t used_bytes_;

    // Metadata cache (in-memory index)
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, CacheEntryInfo> metadata_cache_;

    // Statistics
    mutable CacheStoreStats stats_;
    mutable std::mutex stats_mutex_;

    // Internal helpers
    bool write_metadata_file(const std::string& cache_id, const CacheEntryInfo& info);
    std::optional<CacheEntryInfo> read_metadata_file(const std::string& cache_id) const;
    uint32_t compute_file_checksum(const fs::path& path) const;
    void update_used_bytes() const;
};

/**
 * @brief Factory function for creating file cache stores
 */
inline std::unique_ptr<ICacheStore> create_file_cache_store(
    const std::string& path,
    size_t capacity
) {
    return std::make_unique<FileCacheStore>(fs::path(path), capacity);
}

} // namespace snapllm
