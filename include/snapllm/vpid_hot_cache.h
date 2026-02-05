/**
 * @file vpid_hot_cache.h
 * @brief HOT tier RAM cache for vPID tensors
 *
 * Implements intelligent RAM caching of frequently accessed tensors.
 * Enables DISK = RAM performance by keeping hot tensors in memory.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>
#include <list>
#include <mutex>
#include <atomic>
#include <vector>

namespace snapllm {

/**
 * @brief Cached tensor entry in HOT tier
 */
struct HotCacheEntry {
    std::string model_name;
    std::string tensor_name;
    std::vector<float> data;        // Owned copy in RAM
    size_t access_count;
    uint64_t last_access_time;
    size_t tensor_size_bytes;

    HotCacheEntry() : access_count(0), last_access_time(0), tensor_size_bytes(0) {}
};

/**
 * @brief Statistics for HOT cache
 */
struct HotCacheStats {
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> misses{0};
    std::atomic<uint64_t> evictions{0};
    std::atomic<uint64_t> loads{0};
    std::atomic<size_t> current_size_bytes{0};
    std::atomic<size_t> current_entries{0};

    void reset() {
        hits = 0;
        misses = 0;
        evictions = 0;
        loads = 0;
    }

    double get_hit_rate() const {
        uint64_t total = hits + misses;
        return total > 0 ? (double)hits / total : 0.0;
    }
};

/**
 * @brief HOT tier RAM cache for frequently accessed vPID tensors
 *
 * Implements LRU eviction policy with access frequency tracking.
 * Dramatically improves inference speed by eliminating disk reads
 * for frequently accessed tensors (attention, FFN weights).
 *
 * Key Features:
 * - Configurable RAM budget (default 2-3GB)
 * - LRU eviction when cache full
 * - Access frequency tracking
 * - Thread-safe operations
 * - Transparent integration with vPID
 *
 * Target: 85-90% cache hit rate during inference
 *
 * Example usage:
 * @code
 * VPIDHotCache cache(2ULL * 1024 * 1024 * 1024);  // 2GB budget
 *
 * // Load tensor into cache
 * const float* hot_ptr = cache.get_or_load(
 *     "medicine", "model.layers.0.attention.wq.weight",
 *     disk_ptr, tensor_size
 * );
 *
 * // Statistics
 * std::cout << "Hit rate: " << cache.get_stats().get_hit_rate() * 100 << "%" << std::endl;
 * @endcode
 */
class VPIDHotCache {
public:
    /**
     * @brief Construct HOT cache with RAM budget
     * @param max_size_bytes Maximum RAM to use (default 2GB)
     */
    explicit VPIDHotCache(size_t max_size_bytes = 2ULL * 1024 * 1024 * 1024);

    /**
     * @brief Destructor
     */
    ~VPIDHotCache() = default;

    // Prevent copying
    VPIDHotCache(const VPIDHotCache&) = delete;
    VPIDHotCache& operator=(const VPIDHotCache&) = delete;

    /**
     * @brief Get tensor from cache or load from disk
     * @param model_name Model name
     * @param tensor_name Tensor name
     * @param disk_ptr Pointer to disk-backed data (fallback)
     * @param count Number of float elements
     * @return Pointer to cached data (HOT) or disk data (COLD)
     */
    const float* get_or_load(
        const std::string& model_name,
        const std::string& tensor_name,
        const float* disk_ptr,
        size_t count
    );

    /**
     * @brief Get tensor from cache if present (no load on miss)
     * @param model_name Model name
     * @param tensor_name Tensor name
     * @return Pointer to cached data or nullptr if not in cache
     */
    const float* get_if_cached(
        const std::string& model_name,
        const std::string& tensor_name
    );

    /**
     * @brief Prefetch tensor into cache
     * @param model_name Model name
     * @param tensor_name Tensor name
     * @param disk_ptr Pointer to disk-backed data
     * @param count Number of float elements
     * @return true if loaded into cache
     */
    bool prefetch(
        const std::string& model_name,
        const std::string& tensor_name,
        const float* disk_ptr,
        size_t count
    );

    /**
     * @brief Evict model from cache
     * @param model_name Model to evict
     */
    void evict_model(const std::string& model_name);

    /**
     * @brief Clear entire cache
     */
    void clear();

    /**
     * @brief Get cache statistics
     */
    const HotCacheStats& get_stats() const { return stats_; }

    /**
     * @brief Reset statistics
     */
    void reset_stats() { stats_.reset(); }

    /**
     * @brief Get current cache size
     */
    size_t get_current_size() const { return stats_.current_size_bytes.load(); }

    /**
     * @brief Get maximum cache size
     */
    size_t get_max_size() const { return max_size_bytes_; }

    /**
     * @brief Get number of cached entries
     */
    size_t get_entry_count() const { return stats_.current_entries.load(); }

    /**
     * @brief Get cache utilization (0.0 - 1.0)
     */
    double get_utilization() const {
        return (double)stats_.current_size_bytes.load() / max_size_bytes_;
    }

private:
    // Cache key: "model_name/tensor_name"
    std::string make_key(const std::string& model, const std::string& tensor) const {
        return model + "/" + tensor;
    }

    // LRU list node: cache key
    using LRUList = std::list<std::string>;
    using LRUIterator = LRUList::iterator;

    // Cache storage
    std::unordered_map<std::string, HotCacheEntry> cache_;

    // LRU tracking
    LRUList lru_list_;
    std::unordered_map<std::string, LRUIterator> lru_map_;

    // Configuration
    size_t max_size_bytes_;

    // Statistics
    HotCacheStats stats_;

    // Thread safety
    mutable std::mutex cache_mutex_;

    // Atomic clock for LRU
    std::atomic<uint64_t> access_clock_{0};

    // Helper: Update LRU on access
    void touch(const std::string& key);

    // Helper: Evict least recently used entry
    void evict_lru();

    // Helper: Make space for new entry
    bool make_space(size_t needed_bytes);

    // Helper: Load tensor from disk to cache
    bool load_into_cache(
        const std::string& key,
        const std::string& model_name,
        const std::string& tensor_name,
        const float* disk_ptr,
        size_t count
    );
};

} // namespace snapllm
