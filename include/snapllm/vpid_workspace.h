/**
 * @file vpid_workspace.h
 * @brief Virtual Processing-In-Disk (vPID) Workspace
 *
 * Core vPPE implementation that treats NVMe storage as virtual GPU memory.
 * Enables disk-based computation with RAM-like access patterns.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>

#include "vpid_tensor_cache.h"  // vDPE Direct I/O cache

#ifdef _WIN32
#define NOMINMAX  // Prevent Windows.h from defining min/max macros that interfere with std::min/std::max
#include <windows.h>
#else
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace snapllm {

/**
 * @brief Allocation metadata for vPID buffers
 */
struct VPIDAllocation {
    size_t offset;        ///< Offset in workspace
    size_t size;          ///< Size in bytes
    void* mapped_ptr;     ///< Memory-mapped pointer
    std::string name;     ///< Optional name for debugging
    uint64_t access_count;///< Access frequency tracking
    
    VPIDAllocation() 
        : offset(0), size(0), mapped_ptr(nullptr), access_count(0) {}
        
    VPIDAllocation(size_t off, size_t sz, void* ptr, const std::string& n = "")
        : offset(off), size(sz), mapped_ptr(ptr), name(n), access_count(0) {}
};

/**
 * @brief Statistics for vPID workspace
 */
struct VPIDStats {
    std::atomic<uint64_t> total_allocations{0};
    std::atomic<uint64_t> total_reads{0};
    std::atomic<uint64_t> total_writes{0};
    std::atomic<uint64_t> bytes_read{0};
    std::atomic<uint64_t> bytes_written{0};
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};
    
    void reset() {
        total_allocations = 0;
        total_reads = 0;
        total_writes = 0;
        bytes_read = 0;
        bytes_written = 0;
        cache_hits = 0;
        cache_misses = 0;
    }
    
    double get_hit_rate() const {
        uint64_t total = cache_hits + cache_misses;
        return total > 0 ? (double)cache_hits / total : 0.0;
    }
};

/**
 * @brief Virtual Processing-In-Disk (vPID) Workspace
 * 
 * Manages a reserved disk space (typically 50-100GB) as virtual GPU memory.
 * Provides direct I/O operations, memory mapping, and intelligent caching.
 * 
 * Key Features:
 * - Memory-mapped file I/O for zero-copy access
 * - Direct I/O bypass for predictable performance
 * - Persistent storage across sessions
 * - Thread-safe operations
 * - Access pattern tracking for optimization
 * 
 * Example usage:
 * @code
 * VPIDWorkspace vpid("/path/to/nvme/vpid.bin", 100ULL * 1024 * 1024 * 1024);
 * if (!vpid.initialize()) {
 *     // Handle error
 * }
 * 
 * // Allocate space for dequantized weights
 * auto alloc = vpid.allocate(6ULL * 1024 * 1024 * 1024, "llama3-weights");
 * 
 * // Write data
 * vpid.write_direct(alloc.offset, weights.data(), weights.size());
 * 
 * // Read data (zero-copy via mmap)
 * const float* data = vpid.read_direct<float>(alloc.offset, alloc.size);
 * @endcode
 */
class VPIDWorkspace {
public:
    /**
     * @brief Construct vPID workspace
     * @param workspace_path Path to workspace file (on NVMe recommended)
     * @param total_size Total size in bytes (e.g., 100GB)
     * @param use_direct_io Enable Direct I/O for bypass OS cache
     * @param cache_budget_bytes Tensor cache RAM budget (default: 2GB)
     */
    VPIDWorkspace(const std::string& workspace_path,
                  size_t total_size,
                  bool use_direct_io = false,
                  size_t cache_budget_bytes = 2ULL * 1024 * 1024 * 1024);
    
    /**
     * @brief Destructor - ensures proper cleanup
     */
    ~VPIDWorkspace();
    
    // Prevent copying
    VPIDWorkspace(const VPIDWorkspace&) = delete;
    VPIDWorkspace& operator=(const VPIDWorkspace&) = delete;
    
    /**
     * @brief Initialize the workspace (create/open file, setup mmap)
     * @return true if successful
     */
    bool initialize();
    
    /**
     * @brief Shutdown and cleanup
     */
    void shutdown();
    
    /**
     * @brief Allocate space in workspace
     * @param size Size in bytes
     * @param name Optional name for tracking
     * @return Allocation info
     */
    VPIDAllocation allocate(size_t size, const std::string& name = "");
    
    /**
     * @brief Free allocated space
     * @param alloc Allocation to free
     */
    void free(const VPIDAllocation& alloc);
    
    /**
     * @brief Write data directly to workspace
     * @param offset Offset in workspace
     * @param data Source data
     * @param size Number of bytes
     * @return Number of bytes written
     */
    size_t write_direct(size_t offset, const void* data, size_t size);

    /**
     * @brief Load tensor from disk into cache (vDPE Direct I/O)
     * @param tensor_name Tensor name
     * @param offset Offset in workspace file
     * @param size Size in bytes
     * @return Pointer to cached data, or nullptr if failed
     */
    void* load_tensor_to_cache(const std::string& tensor_name, size_t offset, size_t size);

    /**
     * @brief Get cached tensor pointer (without loading)
     * @param tensor_name Tensor name
     * @return Pointer to cached data, or nullptr if not cached
     */
    void* get_cached_tensor(const std::string& tensor_name);

    /**
     * @brief Evict tensor from cache
     * @param tensor_name Tensor name
     * @return true if evicted successfully
     */
    bool evict_cached_tensor(const std::string& tensor_name);

    /**
     * @brief Get tensor cache statistics
     */
    /**
     * @brief Get direct memory-mapped pointer (bypasses cache, for wiring phase)
     * @param offset Offset in workspace
     * @return Pointer to mmap'd data at offset, or nullptr if not initialized
     *
     * WARNING: This returns a direct pointer to the mmap'd region.
     * Use this ONLY during tensor wiring phase where persistent pointers are needed.
     * For on-demand loading during inference, use read_direct() with cache.
     */
    void* get_mmap_pointer(size_t offset) {
        if (!is_initialized_ || !mapped_region_) {
            fprintf(stderr, "[MMAP DEBUG] get_mmap_pointer(%zu) FAILED: not initialized\n", offset);
            return nullptr;
        }

        // Bounds check
        if (offset >= total_size_) {
            fprintf(stderr, "[MMAP DEBUG] get_mmap_pointer(%zu) FAILED: out of bounds (total=%zu)\n", offset, total_size_);
            return nullptr;
        }

        void* ptr = (uint8_t*)mapped_region_ + offset;
        fprintf(stderr, "[MMAP DEBUG] get_mmap_pointer(%zu) = %p (SUCCESS - direct mmap)\n", offset, ptr);
        return ptr;
    }
    
    /**
     * @brief Check if memory mapping is available
     * @return true if workspace has memory mapping, false otherwise
     */
    bool has_memory_mapping() const {
        return mapped_region_ != nullptr;
    }

    VPIDTensorCache* get_tensor_cache() { return tensor_cache_.get(); }
    
    /**
     * @brief Read data directly from workspace (vDPE: loads to cache on-demand)
     * @tparam T Data type
     * @param offset Offset in workspace
     * @param count Number of elements
     * @param tensor_name Optional tensor name for cache tracking
     * @return Pointer to cached data
     *
     * NOTE: This method now uses Direct I/O with LRU cache.
     * Data is loaded on-demand from disk and cached with fixed RAM budget.
     */
    template<typename T>
    const T* read_direct(size_t offset, size_t count, const std::string& tensor_name = "") {
        if (!is_initialized_) return nullptr;

        size_t byte_offset = offset;
        size_t byte_size = count * sizeof(T);

        // Bounds check
        if (byte_offset + byte_size > total_size_) {
            return nullptr;
        }

        // Update stats
        stats_.total_reads++;
        stats_.bytes_read += byte_size;

        // If no tensor name, load anonymously (fallback for legacy code)
        std::string cache_key = tensor_name.empty() ?
            ("offset_" + std::to_string(offset)) : tensor_name;

        // Load into cache if not already cached
        void* cached_ptr = load_tensor_to_cache(cache_key, byte_offset, byte_size);
        return reinterpret_cast<const T*>(cached_ptr);
    }
    
    /**
     * @brief Synchronize changes to disk
     * @param offset Offset to sync (0 = full sync)
     * @param size Size to sync (0 = full sync)
     */
    void sync(size_t offset = 0, size_t size = 0);
    
    /**
     * @brief Prefetch data into cache
     * @param offset Offset in workspace
     * @param size Number of bytes
     */
    void prefetch(size_t offset, size_t size);
    
    /**
     * @brief Get statistics
     */
    const VPIDStats& get_stats() const { return stats_; }
    
    /**
     * @brief Reset statistics
     */
    void reset_stats() { stats_.reset(); }
    
    /**
     * @brief Check if initialized
     */
    bool is_initialized() const { return is_initialized_; }
    
    /**
     * @brief Get total size
     */
    size_t get_total_size() const { return total_size_; }
    
    /**
     * @brief Get used size
     */
    size_t get_used_size() const { return next_free_offset_; }
    
    /**
     * @brief Get fragmentation ratio
     */
    double get_fragmentation() const;

    /**
     * @brief Register tensor's layer for eviction tracking
     * @param tensor_name Name of tensor (e.g., "blk.5.attn_q.weight")
     * @param offset Offset in workspace
     * @param size Size in bytes
     */
    void register_tensor_layer(const std::string& tensor_name, size_t offset, size_t size);

    /**
     * @brief Evict a specific layer from RAM using DiscardVirtualMemory
     * @param layer_id Layer number to evict (e.g., 0-31 for 32-layer model)
     * @return Number of bytes evicted, or 0 on failure
     */
    size_t evict_layer(int layer_id);

    /**
     * @brief Prefetch a layer into RAM
     * @param layer_id Layer number to prefetch
     * @return Number of bytes prefetched
     */
    size_t prefetch_layer(int layer_id);

    /**
     * @brief Get layer ID from tensor name (e.g., "blk.5.attn_q" → 5)
     * @param tensor_name Tensor name
     * @return Layer ID, or -1 for non-layer tensors (token_embd, output, etc.)
     */
    static int get_layer_from_name(const std::string& tensor_name);

    /**
     * @brief TEST: Evict all memory to verify DiscardVirtualMemory works
     * @return true if eviction succeeded
     */
    bool test_evict_all();
private:
    std::string workspace_path_;
    size_t total_size_;
    bool use_direct_io_;
    size_t cache_budget_bytes_;

#ifdef _WIN32
    HANDLE file_handle_;
    HANDLE mapping_handle_;
#else
    int file_descriptor_;
#endif

    bool is_initialized_;
    void* mapped_region_;

    // vDPE Direct I/O Tensor Cache (replaces memory-mapping)
    std::unique_ptr<VPIDTensorCache> tensor_cache_;

    // Allocation tracking
    std::atomic<size_t> next_free_offset_;
    std::unordered_map<size_t, VPIDAllocation> allocations_;
    std::mutex alloc_mutex_;

    // Statistics
    VPIDStats stats_;

    // Layer-aware eviction tracking
    struct MemoryRegion {
        size_t offset;
        size_t size;
    };
    std::unordered_map<int, std::vector<MemoryRegion>> layer_regions_;  // layer_id -> regions
    std::mutex layer_mutex_;

    // Platform-specific helpers
    bool create_or_open_file();
    void cleanup_resources();

    /**
     * @brief Read data from disk using Direct I/O
     * @param offset Offset in file
     * @param buffer Destination buffer (must be page-aligned)
     * @param size Number of bytes to read
     * @return Number of bytes read, or 0 on error
     */
    size_t read_direct_io(size_t offset, void* buffer, size_t size);
};

} // namespace snapllm
