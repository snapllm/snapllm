/**
 * @file vpid_tensor_cache.h
 * @brief vDPE Tensor Cache with LRU Eviction (Direct I/O)
 *
 * Manages a fixed RAM budget for caching tensors loaded from disk.
 * Uses Direct I/O to bypass OS page cache for predictable RAM usage.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <list>

#ifdef _WIN32
#define NOMINMAX  // Prevent Windows.h from defining min/max macros
#include <windows.h>
#endif

namespace snapllm {

/**
 * @brief Cached tensor entry
 */
struct CachedTensor {
    std::string name;
    void* data_ptr;           ///< Aligned buffer for Direct I/O
    size_t size;              ///< Size in bytes
    uint64_t last_access_time; ///< For LRU eviction
    uint64_t access_count;     ///< Access frequency

    CachedTensor()
        : data_ptr(nullptr), size(0), last_access_time(0), access_count(0) {}

    CachedTensor(const std::string& n, void* ptr, size_t sz)
        : name(n), data_ptr(ptr), size(sz), last_access_time(0), access_count(0) {}
};

/**
 * @brief LRU cache for vDPE tensors with Direct I/O
 *
 * Maintains a fixed RAM budget and evicts least-recently-used tensors
 * when the cache is full. Uses Direct I/O for predictable performance.
 */
class VPIDTensorCache {
public:
    /**
     * @brief Construct tensor cache
     * @param cache_budget_bytes Maximum RAM to use (e.g., 2GB)
     */
    explicit VPIDTensorCache(size_t cache_budget_bytes);

    /**
     * @brief Destructor - frees all cached tensors
     */
    ~VPIDTensorCache();

    // Prevent copying
    VPIDTensorCache(const VPIDTensorCache&) = delete;
    VPIDTensorCache& operator=(const VPIDTensorCache&) = delete;

    /**
     * @brief Allocate cache slot for tensor
     * @param name Tensor name
     * @param size Size in bytes
     * @return Pointer to allocated buffer (aligned for Direct I/O), or nullptr if failed
     */
    void* allocate_slot(const std::string& name, size_t size);

    /**
     * @brief Get cached tensor pointer
     * @param name Tensor name
     * @return Pointer to cached data, or nullptr if not cached
     */
    void* get_tensor(const std::string& name);

    /**
     * @brief Check if tensor is cached
     * @param name Tensor name
     * @return true if cached
     */
    bool is_cached(const std::string& name) const;

    /**
     * @brief Evict specific tensor from cache
     * @param name Tensor name
     * @return true if evicted successfully
     */
    bool evict_tensor(const std::string& name);

    /**
     * @brief Evict least-recently-used tensor
     * @return Size freed in bytes, or 0 if cache is empty
     */
    size_t evict_lru();

    /**
     * @brief Evict tensors until we have enough free space
     * @param required_bytes Bytes needed
     * @return true if enough space was freed
     */
    bool evict_until_free(size_t required_bytes);

    /**
     * @brief Clear all cached tensors
     */
    void clear_all();

    /**
     * @brief Get cache statistics
     */
    size_t get_used_bytes() const { return used_bytes_; }
    size_t get_budget_bytes() const { return budget_bytes_; }
    size_t get_cached_count() const { return cache_.size(); }
    double get_utilization() const {
        return budget_bytes_ > 0 ? (double)used_bytes_ / budget_bytes_ : 0.0;
    }

    /**
     * @brief Get cache hit rate
     */
    double get_hit_rate() const {
        uint64_t total = cache_hits_ + cache_misses_;
        return total > 0 ? (double)cache_hits_ / total : 0.0;
    }

    /**
     * @brief Get all cached tensor names
     * @return Vector of tensor names currently in cache
     */
    std::vector<std::string> get_all_cached_names() const;

    /**
     * @brief Get size of a cached tensor
     * @param name Tensor name
     * @return Size in bytes, or 0 if not found
     */
    size_t get_tensor_size(const std::string& name) const;

private:
    size_t budget_bytes_;           ///< Maximum RAM budget
    size_t used_bytes_;             ///< Current RAM usage

    // Cache: tensor_name -> CachedTensor
    std::unordered_map<std::string, CachedTensor> cache_;

    // LRU tracking: list of tensor names, ordered by access time (oldest first)
    std::list<std::string> lru_list_;
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_map_;

    // Thread safety
    mutable std::mutex cache_mutex_;

    // Statistics
    uint64_t cache_hits_;
    uint64_t cache_misses_;
    uint64_t eviction_count_;

    // Current timestamp counter
    uint64_t current_time_;

    /**
     * @brief Update LRU tracking for tensor access
     */
    void update_lru(const std::string& name);

    /**
     * @brief Allocate aligned buffer for Direct I/O
     * @param size Size in bytes
     * @return Aligned pointer, or nullptr if failed
     */
    void* allocate_aligned_buffer(size_t size);

    /**
     * @brief Free aligned buffer
     */
    void free_aligned_buffer(void* ptr, size_t size);
};

} // namespace snapllm
