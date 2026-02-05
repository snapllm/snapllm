/**
 * @file i_memory_allocator.h
 * @brief Interface for Memory Allocation with Tiering Support
 *
 * Defines the contract for memory allocators that support:
 * - Multi-tier storage (GPU HBM, CPU RAM, SSD)
 * - Automatic promotion/demotion based on access patterns
 * - Memory pressure handling and eviction
 *
 * Design Principles:
 * - Tier-aware allocation with fallback
 * - Non-blocking allocation attempts
 * - Observable memory state
 * - LRU-based eviction
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <optional>
#include <vector>
#include <functional>
#include <chrono>

namespace snapllm {

/**
 * @brief Memory tier enumeration
 *
 * Ordered from fastest/smallest to slowest/largest.
 * Higher numeric value = faster tier.
 */
enum class MemoryTier {
    SSD_NVME = 0,   ///< Cold: Persistent NVMe storage (slowest, largest)
    CPU_RAM = 1,    ///< Warm: System RAM (fast, large)
    GPU_HBM = 2     ///< Hot: GPU High Bandwidth Memory (fastest, limited)
};

/**
 * @brief Convert MemoryTier to string
 */
inline const char* memory_tier_to_string(MemoryTier tier) {
    switch (tier) {
        case MemoryTier::GPU_HBM: return "GPU_HBM";
        case MemoryTier::CPU_RAM: return "CPU_RAM";
        case MemoryTier::SSD_NVME: return "SSD_NVME";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Memory block descriptor
 *
 * Represents an allocated memory region with tracking metadata.
 */
struct MemoryBlock {
    void* ptr = nullptr;           ///< Pointer to allocated memory
    size_t size = 0;               ///< Size in bytes
    MemoryTier tier = MemoryTier::CPU_RAM;  ///< Current storage tier
    std::string owner_id;          ///< ID of owning resource

    // Access tracking for LRU
    uint64_t access_count = 0;
    std::chrono::steady_clock::time_point last_access;
    std::chrono::steady_clock::time_point created_at;

    MemoryBlock() : created_at(std::chrono::steady_clock::now()),
                    last_access(std::chrono::steady_clock::now()) {}

    MemoryBlock(void* p, size_t s, MemoryTier t, const std::string& owner)
        : ptr(p), size(s), tier(t), owner_id(owner),
          created_at(std::chrono::steady_clock::now()),
          last_access(std::chrono::steady_clock::now()) {}

    bool is_valid() const { return ptr != nullptr && size > 0; }
};

/**
 * @brief Tier capacity and usage statistics
 */
struct TierStats {
    MemoryTier tier;
    size_t capacity_bytes = 0;     ///< Total capacity
    size_t used_bytes = 0;         ///< Currently used
    size_t available_bytes = 0;    ///< Available for allocation
    size_t item_count = 0;         ///< Number of allocations
    double hit_rate = 0.0;         ///< Cache hit rate for this tier
    uint64_t promotions = 0;       ///< Items promoted to this tier
    uint64_t demotions = 0;        ///< Items demoted from this tier

    double utilization() const {
        return capacity_bytes > 0 ? (double)used_bytes / capacity_bytes : 0.0;
    }
};

/**
 * @brief Aggregate memory statistics
 */
struct MemoryStats {
    TierStats gpu;
    TierStats cpu;
    TierStats ssd;

    uint64_t total_allocations = 0;
    uint64_t total_deallocations = 0;
    uint64_t total_promotions = 0;
    uint64_t total_demotions = 0;
    uint64_t total_evictions = 0;

    size_t total_capacity() const {
        return gpu.capacity_bytes + cpu.capacity_bytes + ssd.capacity_bytes;
    }

    size_t total_used() const {
        return gpu.used_bytes + cpu.used_bytes + ssd.used_bytes;
    }
};

/**
 * @brief Memory allocation result
 */
struct AllocationResult {
    bool success = false;
    MemoryBlock block;
    std::string error_message;

    static AllocationResult ok(const MemoryBlock& b) {
        return {true, b, ""};
    }

    static AllocationResult fail(const std::string& msg) {
        return {false, MemoryBlock{}, msg};
    }
};

/**
 * @brief Eviction policy enumeration
 */
enum class EvictionPolicy {
    LRU,            ///< Least Recently Used
    LFU,            ///< Least Frequently Used
    FIFO,           ///< First In First Out
    SIZE_WEIGHTED   ///< LRU weighted by size (evict large cold items first)
};

/**
 * @brief Interface for tiered memory allocator
 *
 * Contract:
 * - allocate() returns nullopt if allocation fails (never throws)
 * - Returned MemoryBlock.ptr is valid until deallocate() is called
 * - available(tier) + used(tier) <= capacity(tier)
 * - promote/demote preserve data content exactly
 * - deallocate() of same block twice is undefined behavior
 *
 * Thread Safety:
 * - All methods are thread-safe
 * - Concurrent allocate() calls may race; some may fail even with space
 * - allocate() and deallocate() for same owner_id are serialized
 */
class IMemoryAllocator {
public:
    virtual ~IMemoryAllocator() = default;

    //=========================================================================
    // Allocation Operations
    //=========================================================================

    /**
     * @brief Allocate memory in preferred tier with automatic fallback
     * @param size Size in bytes
     * @param preferred_tier Preferred storage tier
     * @param owner_id ID of owning resource for tracking
     * @return Allocation result with block if successful
     *
     * Allocation strategy:
     * 1. Try preferred_tier
     * 2. If full, try eviction to make space
     * 3. If eviction fails, try lower tiers
     * 4. Return failure if all tiers exhausted
     */
    virtual AllocationResult allocate(
        size_t size,
        MemoryTier preferred_tier,
        const std::string& owner_id
    ) = 0;

    /**
     * @brief Deallocate memory block
     * @param block Block to deallocate
     */
    virtual void deallocate(const MemoryBlock& block) = 0;

    /**
     * @brief Deallocate all blocks owned by a specific owner
     * @param owner_id Owner ID
     * @return Number of blocks deallocated
     */
    virtual size_t deallocate_owner(const std::string& owner_id) = 0;

    //=========================================================================
    // Tiering Operations
    //=========================================================================

    /**
     * @brief Promote allocation to a faster tier
     * @param owner_id Owner ID of allocation
     * @param target_tier Target tier (must be faster than current)
     * @return true if promotion succeeded
     *
     * Data is copied to new tier, old allocation freed.
     */
    virtual bool promote(const std::string& owner_id, MemoryTier target_tier) = 0;

    /**
     * @brief Demote allocation to a slower tier
     * @param owner_id Owner ID of allocation
     * @param target_tier Target tier (must be slower than current)
     * @return true if demotion succeeded
     *
     * Data is copied to new tier, old allocation freed.
     */
    virtual bool demote(const std::string& owner_id, MemoryTier target_tier) = 0;

    /**
     * @brief Get current tier of an allocation
     * @param owner_id Owner ID
     * @return Current tier, or nullopt if not found
     */
    virtual std::optional<MemoryTier> get_tier(const std::string& owner_id) const = 0;

    //=========================================================================
    // Memory Pressure Handling
    //=========================================================================

    /**
     * @brief Request eviction to free space
     * @param bytes_needed Bytes to free
     * @param tier Tier to free from
     * @return Bytes actually freed
     */
    virtual size_t evict(size_t bytes_needed, MemoryTier tier) = 0;

    /**
     * @brief Set eviction policy
     * @param policy Eviction policy to use
     */
    virtual void set_eviction_policy(EvictionPolicy policy) = 0;

    /**
     * @brief Get current eviction policy
     */
    virtual EvictionPolicy get_eviction_policy() const = 0;

    //=========================================================================
    // Query Operations
    //=========================================================================

    /**
     * @brief Get available space in tier
     * @param tier Memory tier
     * @return Available bytes
     */
    virtual size_t available(MemoryTier tier) const = 0;

    /**
     * @brief Get used space in tier
     * @param tier Memory tier
     * @return Used bytes
     */
    virtual size_t used(MemoryTier tier) const = 0;

    /**
     * @brief Get total capacity of tier
     * @param tier Memory tier
     * @return Capacity in bytes
     */
    virtual size_t capacity(MemoryTier tier) const = 0;

    /**
     * @brief Get allocation by owner ID
     * @param owner_id Owner ID
     * @return Memory block if found
     */
    virtual std::optional<MemoryBlock> get_block(const std::string& owner_id) const = 0;

    /**
     * @brief Get all allocations in a tier
     * @param tier Memory tier
     * @return Vector of memory blocks
     */
    virtual std::vector<MemoryBlock> get_blocks_in_tier(MemoryTier tier) const = 0;

    //=========================================================================
    // Statistics
    //=========================================================================

    /**
     * @brief Get memory statistics
     * @return Aggregate statistics
     */
    virtual MemoryStats get_stats() const = 0;

    /**
     * @brief Get statistics for specific tier
     * @param tier Memory tier
     * @return Tier statistics
     */
    virtual TierStats get_tier_stats(MemoryTier tier) const = 0;

    /**
     * @brief Reset statistics counters
     */
    virtual void reset_stats() = 0;

    //=========================================================================
    // Access Tracking (for LRU)
    //=========================================================================

    /**
     * @brief Record access to allocation
     * @param owner_id Owner ID
     *
     * Call this when an allocation is used to update LRU tracking.
     */
    virtual void record_access(const std::string& owner_id) = 0;
};

/**
 * @brief Callback for eviction events
 */
using EvictionCallback = std::function<void(const std::string& owner_id, MemoryTier from_tier)>;

/**
 * @brief Extended interface with eviction notifications
 */
class IObservableMemoryAllocator : public IMemoryAllocator {
public:
    /**
     * @brief Register callback for eviction events
     * @param callback Function called before eviction
     * @return Subscription ID
     */
    virtual uint64_t on_eviction(EvictionCallback callback) = 0;

    /**
     * @brief Unregister eviction callback
     * @param subscription_id ID from on_eviction
     */
    virtual void remove_eviction_callback(uint64_t subscription_id) = 0;
};

} // namespace snapllm
