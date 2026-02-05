/**
 * @file tiered_memory_allocator.h
 * @brief Tiered Memory Allocator for vPID L2
 *
 * Implements IMemoryAllocator with three-tier storage:
 * - GPU HBM (hot): CUDA device memory for fastest access
 * - CPU RAM (warm): Pinned host memory for fast GPU transfers
 * - SSD NVMe (cold): Delegated to file-based storage
 *
 * Key Features:
 * - Automatic fallback to lower tiers when preferred tier is full
 * - LRU-based eviction with configurable policies
 * - Thread-safe allocation and deallocation
 * - Memory pressure handling with callbacks
 */

#pragma once

#include "interfaces/i_memory_allocator.h"
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <atomic>
#include <vector>

namespace snapllm {

/**
 * @brief Configuration for TieredMemoryAllocator
 */
struct TieredAllocatorConfig {
    // Tier capacities (0 = auto-detect)
    size_t gpu_capacity_bytes = 0;      ///< GPU HBM capacity (0 = detect available VRAM)
    size_t cpu_capacity_bytes = 0;      ///< CPU RAM capacity (0 = 50% of system RAM)
    size_t ssd_capacity_bytes = 0;      ///< SSD capacity (0 = unlimited)

    // Thresholds
    double eviction_threshold = 0.85;   ///< Start eviction when tier reaches this utilization
    double target_utilization = 0.70;   ///< Evict down to this level

    // Alignment
    size_t alignment = 256;             ///< Memory alignment for allocations

    // CUDA settings
    int cuda_device = 0;                ///< CUDA device ID
    bool use_pinned_memory = true;      ///< Use CUDA pinned memory for CPU tier

    // Default config
    static TieredAllocatorConfig defaults() {
        return TieredAllocatorConfig{};
    }
};

/**
 * @brief Tiered Memory Allocator
 *
 * Thread-safe implementation of IMemoryAllocator with GPU/CPU/SSD tiering.
 *
 * Usage:
 * @code
 * TieredAllocatorConfig config;
 * config.gpu_capacity_bytes = 6ULL * 1024 * 1024 * 1024;  // 6GB
 * config.cpu_capacity_bytes = 16ULL * 1024 * 1024 * 1024; // 16GB
 *
 * TieredMemoryAllocator allocator(config);
 *
 * // Allocate in GPU (hot tier)
 * auto result = allocator.allocate(100 * 1024 * 1024, MemoryTier::GPU_HBM, "context_123");
 * if (result.success) {
 *     // Use result.block.ptr
 * }
 *
 * // Later, demote to CPU if memory pressure
 * allocator.demote("context_123", MemoryTier::CPU_RAM);
 * @endcode
 */
class TieredMemoryAllocator : public IObservableMemoryAllocator {
public:
    /**
     * @brief Construct allocator with configuration
     * @param config Allocator configuration
     */
    explicit TieredMemoryAllocator(const TieredAllocatorConfig& config = TieredAllocatorConfig::defaults());

    /**
     * @brief Destructor - frees all allocated memory
     */
    ~TieredMemoryAllocator() override;

    // Non-copyable
    TieredMemoryAllocator(const TieredMemoryAllocator&) = delete;
    TieredMemoryAllocator& operator=(const TieredMemoryAllocator&) = delete;

    //=========================================================================
    // IMemoryAllocator Implementation
    //=========================================================================

    AllocationResult allocate(
        size_t size,
        MemoryTier preferred_tier,
        const std::string& owner_id
    ) override;

    void deallocate(const MemoryBlock& block) override;
    size_t deallocate_owner(const std::string& owner_id) override;

    bool promote(const std::string& owner_id, MemoryTier target_tier) override;
    bool demote(const std::string& owner_id, MemoryTier target_tier) override;
    std::optional<MemoryTier> get_tier(const std::string& owner_id) const override;

    size_t evict(size_t bytes_needed, MemoryTier tier) override;
    void set_eviction_policy(EvictionPolicy policy) override;
    EvictionPolicy get_eviction_policy() const override;

    size_t available(MemoryTier tier) const override;
    size_t used(MemoryTier tier) const override;
    size_t capacity(MemoryTier tier) const override;

    std::optional<MemoryBlock> get_block(const std::string& owner_id) const override;
    std::vector<MemoryBlock> get_blocks_in_tier(MemoryTier tier) const override;

    MemoryStats get_stats() const override;
    TierStats get_tier_stats(MemoryTier tier) const override;
    void reset_stats() override;

    void record_access(const std::string& owner_id) override;

    //=========================================================================
    // IObservableMemoryAllocator Implementation
    //=========================================================================

    uint64_t on_eviction(EvictionCallback callback) override;
    void remove_eviction_callback(uint64_t subscription_id) override;

    //=========================================================================
    // Additional Methods
    //=========================================================================

    /**
     * @brief Check if CUDA is available
     */
    bool cuda_available() const { return cuda_available_; }

    /**
     * @brief Get configuration
     */
    const TieredAllocatorConfig& get_config() const { return config_; }

private:
    TieredAllocatorConfig config_;
    bool cuda_available_ = false;

    // Per-tier storage
    struct TierStorage {
        size_t capacity = 0;
        std::atomic<size_t> used{0};
        std::atomic<uint64_t> allocations{0};
        std::atomic<uint64_t> deallocations{0};
        std::atomic<uint64_t> promotions{0};
        std::atomic<uint64_t> demotions{0};
        std::atomic<uint64_t> hits{0};
        std::atomic<uint64_t> accesses{0};
    };

    TierStorage gpu_storage_;
    TierStorage cpu_storage_;
    TierStorage ssd_storage_;

    // Block tracking
    mutable std::shared_mutex blocks_mutex_;
    std::unordered_map<std::string, MemoryBlock> blocks_;  // owner_id -> block

    // Eviction callbacks
    mutable std::mutex callbacks_mutex_;
    std::unordered_map<uint64_t, EvictionCallback> eviction_callbacks_;
    std::atomic<uint64_t> next_callback_id_{0};

    // Eviction policy
    std::atomic<EvictionPolicy> eviction_policy_{EvictionPolicy::LRU};

    // Global stats
    std::atomic<uint64_t> total_evictions_{0};

    //=========================================================================
    // Internal Methods
    //=========================================================================

    /**
     * @brief Initialize CUDA if available
     */
    void init_cuda();

    /**
     * @brief Detect and set tier capacities
     */
    void detect_capacities();

    /**
     * @brief Allocate raw memory in specific tier
     */
    void* allocate_raw(size_t size, MemoryTier tier);

    /**
     * @brief Free raw memory
     */
    void free_raw(void* ptr, size_t size, MemoryTier tier);

    /**
     * @brief Copy data between tiers
     */
    bool copy_between_tiers(void* dst, MemoryTier dst_tier,
                            const void* src, MemoryTier src_tier,
                            size_t size);

    /**
     * @brief Get storage for tier
     */
    TierStorage& get_storage(MemoryTier tier);
    const TierStorage& get_storage(MemoryTier tier) const;

    /**
     * @brief Select eviction candidates based on policy
     */
    std::vector<std::string> select_eviction_candidates(
        MemoryTier tier,
        size_t bytes_needed
    );

    /**
     * @brief Notify eviction callbacks
     */
    void notify_eviction(const std::string& owner_id, MemoryTier tier);

    /**
     * @brief Align size to configured alignment
     */
    size_t align_size(size_t size) const;
};

} // namespace snapllm
