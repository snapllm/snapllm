/**
 * @file tiered_memory_allocator.cpp
 * @brief Tiered Memory Allocator Implementation
 *
 * Implements GPU/CPU/SSD tiered memory allocation for vPID L2.
 */

#include "snapllm/tiered_memory_allocator.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <mutex>

// Platform-specific aligned allocation
#ifdef _MSC_VER
#include <malloc.h>
#define ALIGNED_ALLOC(alignment, size) _aligned_malloc(size, alignment)
#define ALIGNED_FREE(ptr) _aligned_free(ptr)
#else
#define ALIGNED_ALLOC(alignment, size) std::aligned_alloc(alignment, size)
#define ALIGNED_FREE(ptr) std::free(ptr)
#endif

// CUDA support (conditional)
#ifdef __CUDACC__
#include <cuda_runtime.h>
#define CUDA_AVAILABLE 1
#else
// Try to include CUDA headers if available at compile time
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

// Forward declarations for CUDA functions (loaded dynamically or linked)
#ifdef GGML_USE_CUDA
extern "C" {
    int cudaSetDevice(int device);
    int cudaMalloc(void** devPtr, size_t size);
    int cudaFree(void* devPtr);
    int cudaMallocHost(void** ptr, size_t size);
    int cudaFreeHost(void* ptr);
    int cudaMemcpy(void* dst, const void* src, size_t count, int kind);
    int cudaMemGetInfo(size_t* free, size_t* total);
    int cudaGetDeviceCount(int* count);
}
#define CUDA_AVAILABLE 1
#define cudaSuccess 0
#define cudaMemcpyHostToDevice 1
#define cudaMemcpyDeviceToHost 2
#define cudaMemcpyDeviceToDevice 3
#define cudaMemcpyHostToHost 0
#else
#define CUDA_AVAILABLE 0
#endif
#endif

namespace snapllm {

//=============================================================================
// Constructor / Destructor
//=============================================================================

TieredMemoryAllocator::TieredMemoryAllocator(const TieredAllocatorConfig& config)
    : config_(config)
{
    std::cout << "[TieredMemoryAllocator] Initializing..." << std::endl;

    // Initialize CUDA if available
    init_cuda();

    // Detect and set capacities
    detect_capacities();

    std::cout << "[TieredMemoryAllocator] Initialized with capacities:" << std::endl;
    std::cout << "  GPU: " << (gpu_storage_.capacity / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "  CPU: " << (cpu_storage_.capacity / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "  SSD: " << (ssd_storage_.capacity / (1024 * 1024 * 1024)) << " GB" << std::endl;
}

TieredMemoryAllocator::~TieredMemoryAllocator() {
    std::cout << "[TieredMemoryAllocator] Shutting down, freeing all allocations..." << std::endl;

    // Free all allocated blocks
    std::unique_lock<std::shared_mutex> lock(blocks_mutex_);

    for (auto& [owner_id, block] : blocks_) {
        if (block.ptr) {
            free_raw(block.ptr, block.size, block.tier);
        }
    }
    blocks_.clear();

    std::cout << "[TieredMemoryAllocator] Shutdown complete" << std::endl;
}

//=============================================================================
// Initialization
//=============================================================================

void TieredMemoryAllocator::init_cuda() {
#if CUDA_AVAILABLE
    int device_count = 0;
    int result = cudaGetDeviceCount(&device_count);

    if (result == cudaSuccess && device_count > 0) {
        result = cudaSetDevice(config_.cuda_device);
        if (result == cudaSuccess) {
            cuda_available_ = true;
            std::cout << "[TieredMemoryAllocator] CUDA initialized on device "
                      << config_.cuda_device << std::endl;
        }
    }

    if (!cuda_available_) {
        std::cout << "[TieredMemoryAllocator] CUDA not available, GPU tier disabled" << std::endl;
    }
#else
    std::cout << "[TieredMemoryAllocator] Built without CUDA support, GPU tier disabled" << std::endl;
    cuda_available_ = false;
#endif
}

void TieredMemoryAllocator::detect_capacities() {
    // GPU capacity
    if (config_.gpu_capacity_bytes > 0) {
        gpu_storage_.capacity = config_.gpu_capacity_bytes;
    } else if (cuda_available_) {
#if CUDA_AVAILABLE
        size_t free_mem = 0, total_mem = 0;
        if (cudaMemGetInfo(&free_mem, &total_mem) == cudaSuccess) {
            // Use 80% of free VRAM
            gpu_storage_.capacity = static_cast<size_t>(free_mem * 0.8);
        }
#endif
    }

    // CPU capacity
    if (config_.cpu_capacity_bytes > 0) {
        cpu_storage_.capacity = config_.cpu_capacity_bytes;
    } else {
        // Default to 8GB for CPU tier
        cpu_storage_.capacity = 8ULL * 1024 * 1024 * 1024;
    }

    // SSD capacity (effectively unlimited for file storage)
    if (config_.ssd_capacity_bytes > 0) {
        ssd_storage_.capacity = config_.ssd_capacity_bytes;
    } else {
        // Default to 100GB for SSD tier
        ssd_storage_.capacity = 100ULL * 1024 * 1024 * 1024;
    }
}

//=============================================================================
// Allocation Operations
//=============================================================================

AllocationResult TieredMemoryAllocator::allocate(
    size_t size,
    MemoryTier preferred_tier,
    const std::string& owner_id
) {
    if (size == 0) {
        return AllocationResult::fail("Cannot allocate zero bytes");
    }

    // Check if owner already has an allocation
    {
        std::shared_lock<std::shared_mutex> lock(blocks_mutex_);
        if (blocks_.find(owner_id) != blocks_.end()) {
            return AllocationResult::fail("Owner already has an allocation: " + owner_id);
        }
    }

    size_t aligned_size = align_size(size);

    // Try preferred tier first
    std::vector<MemoryTier> tiers_to_try;

    // Build tier fallback order based on preference
    switch (preferred_tier) {
        case MemoryTier::GPU_HBM:
            if (cuda_available_) {
                tiers_to_try = {MemoryTier::GPU_HBM, MemoryTier::CPU_RAM};
            } else {
                tiers_to_try = {MemoryTier::CPU_RAM};
            }
            break;
        case MemoryTier::CPU_RAM:
            tiers_to_try = {MemoryTier::CPU_RAM};
            break;
        case MemoryTier::SSD_NVME:
            // SSD tier is handled by file storage, not direct allocation
            return AllocationResult::fail("SSD tier requires file-based storage");
    }

    for (MemoryTier tier : tiers_to_try) {
        TierStorage& storage = get_storage(tier);

        // Check if there's enough space
        size_t current_used = storage.used.load();
        if (current_used + aligned_size > storage.capacity) {
            // Try eviction
            size_t needed = aligned_size - (storage.capacity - current_used);
            size_t freed = evict(needed, tier);

            if (freed < needed) {
                continue;  // Try next tier
            }
        }

        // Allocate
        void* ptr = allocate_raw(aligned_size, tier);
        if (!ptr) {
            continue;  // Try next tier
        }

        // Create block
        MemoryBlock block(ptr, aligned_size, tier, owner_id);

        // Store block
        {
            std::unique_lock<std::shared_mutex> lock(blocks_mutex_);
            blocks_[owner_id] = block;
        }

        // Update stats
        storage.used.fetch_add(aligned_size);
        storage.allocations.fetch_add(1);

        std::cout << "[TieredMemoryAllocator] Allocated " << (aligned_size / 1024) << " KB in "
                  << memory_tier_to_string(tier) << " for '" << owner_id << "'" << std::endl;

        return AllocationResult::ok(block);
    }

    return AllocationResult::fail("Failed to allocate " + std::to_string(size) +
                                   " bytes in any tier for " + owner_id);
}

void TieredMemoryAllocator::deallocate(const MemoryBlock& block) {
    if (!block.is_valid()) {
        return;
    }

    deallocate_owner(block.owner_id);
}

size_t TieredMemoryAllocator::deallocate_owner(const std::string& owner_id) {
    std::unique_lock<std::shared_mutex> lock(blocks_mutex_);

    auto it = blocks_.find(owner_id);
    if (it == blocks_.end()) {
        return 0;
    }

    MemoryBlock& block = it->second;
    size_t size = block.size;
    MemoryTier tier = block.tier;

    // Free the memory
    free_raw(block.ptr, block.size, tier);

    // Update stats
    TierStorage& storage = get_storage(tier);
    storage.used.fetch_sub(size);
    storage.deallocations.fetch_add(1);

    // Remove from map
    blocks_.erase(it);

    std::cout << "[TieredMemoryAllocator] Deallocated " << (size / 1024) << " KB from "
              << memory_tier_to_string(tier) << " for '" << owner_id << "'" << std::endl;

    return 1;
}

//=============================================================================
// Tiering Operations
//=============================================================================

bool TieredMemoryAllocator::promote(const std::string& owner_id, MemoryTier target_tier) {
    std::unique_lock<std::shared_mutex> lock(blocks_mutex_);

    auto it = blocks_.find(owner_id);
    if (it == blocks_.end()) {
        return false;
    }

    MemoryBlock& block = it->second;

    // Check valid promotion (to faster tier)
    if (static_cast<int>(target_tier) <= static_cast<int>(block.tier)) {
        return false;  // Can only promote to faster tier
    }

    // GPU promotion requires CUDA
    if (target_tier == MemoryTier::GPU_HBM && !cuda_available_) {
        return false;
    }

    TierStorage& target_storage = get_storage(target_tier);

    // Check space
    if (target_storage.used.load() + block.size > target_storage.capacity) {
        // Try eviction
        size_t needed = block.size - (target_storage.capacity - target_storage.used.load());
        lock.unlock();
        size_t freed = evict(needed, target_tier);
        lock.lock();

        // Re-find block (may have changed)
        it = blocks_.find(owner_id);
        if (it == blocks_.end()) {
            return false;
        }

        if (freed < needed) {
            return false;
        }
    }

    // Allocate in target tier
    void* new_ptr = allocate_raw(block.size, target_tier);
    if (!new_ptr) {
        return false;
    }

    // Copy data
    if (!copy_between_tiers(new_ptr, target_tier, block.ptr, block.tier, block.size)) {
        free_raw(new_ptr, block.size, target_tier);
        return false;
    }

    // Update stats
    TierStorage& old_storage = get_storage(block.tier);
    old_storage.used.fetch_sub(block.size);
    old_storage.demotions.fetch_add(1);

    target_storage.used.fetch_add(block.size);
    target_storage.promotions.fetch_add(1);

    // Free old memory
    free_raw(block.ptr, block.size, block.tier);

    // Update block
    block.ptr = new_ptr;
    MemoryTier old_tier = block.tier;
    block.tier = target_tier;
    block.last_access = std::chrono::steady_clock::now();

    std::cout << "[TieredMemoryAllocator] Promoted '" << owner_id << "' from "
              << memory_tier_to_string(old_tier) << " to "
              << memory_tier_to_string(target_tier) << std::endl;

    return true;
}

bool TieredMemoryAllocator::demote(const std::string& owner_id, MemoryTier target_tier) {
    std::unique_lock<std::shared_mutex> lock(blocks_mutex_);

    auto it = blocks_.find(owner_id);
    if (it == blocks_.end()) {
        return false;
    }

    MemoryBlock& block = it->second;

    // Check valid demotion (to slower tier)
    if (static_cast<int>(target_tier) >= static_cast<int>(block.tier)) {
        return false;  // Can only demote to slower tier
    }

    // SSD demotion is handled differently (file-based)
    if (target_tier == MemoryTier::SSD_NVME) {
        // For SSD, we just free the memory (data should be saved to disk separately)
        notify_eviction(owner_id, block.tier);

        TierStorage& old_storage = get_storage(block.tier);
        old_storage.used.fetch_sub(block.size);
        old_storage.demotions.fetch_add(1);

        free_raw(block.ptr, block.size, block.tier);
        block.ptr = nullptr;
        block.tier = target_tier;

        std::cout << "[TieredMemoryAllocator] Demoted '" << owner_id << "' to SSD (memory freed)" << std::endl;
        return true;
    }

    TierStorage& target_storage = get_storage(target_tier);

    // Check space
    if (target_storage.used.load() + block.size > target_storage.capacity) {
        return false;  // No space in target tier
    }

    // Allocate in target tier
    void* new_ptr = allocate_raw(block.size, target_tier);
    if (!new_ptr) {
        return false;
    }

    // Copy data
    if (!copy_between_tiers(new_ptr, target_tier, block.ptr, block.tier, block.size)) {
        free_raw(new_ptr, block.size, target_tier);
        return false;
    }

    // Update stats
    TierStorage& old_storage = get_storage(block.tier);
    old_storage.used.fetch_sub(block.size);
    old_storage.demotions.fetch_add(1);

    target_storage.used.fetch_add(block.size);

    // Free old memory
    free_raw(block.ptr, block.size, block.tier);

    // Update block
    block.ptr = new_ptr;
    MemoryTier old_tier = block.tier;
    block.tier = target_tier;

    std::cout << "[TieredMemoryAllocator] Demoted '" << owner_id << "' from "
              << memory_tier_to_string(old_tier) << " to "
              << memory_tier_to_string(target_tier) << std::endl;

    return true;
}

std::optional<MemoryTier> TieredMemoryAllocator::get_tier(const std::string& owner_id) const {
    std::shared_lock<std::shared_mutex> lock(blocks_mutex_);

    auto it = blocks_.find(owner_id);
    if (it == blocks_.end()) {
        return std::nullopt;
    }

    return it->second.tier;
}

//=============================================================================
// Eviction
//=============================================================================

size_t TieredMemoryAllocator::evict(size_t bytes_needed, MemoryTier tier) {
    if (bytes_needed == 0) {
        return 0;
    }

    // Select candidates
    auto candidates = select_eviction_candidates(tier, bytes_needed);
    if (candidates.empty()) {
        return 0;
    }

    size_t freed = 0;

    for (const auto& owner_id : candidates) {
        if (freed >= bytes_needed) {
            break;
        }

        // Notify callbacks before eviction
        notify_eviction(owner_id, tier);

        // Try to demote to next lower tier
        MemoryTier lower_tier;
        if (tier == MemoryTier::GPU_HBM) {
            lower_tier = MemoryTier::CPU_RAM;
        } else {
            lower_tier = MemoryTier::SSD_NVME;
        }

        std::shared_lock<std::shared_mutex> lock(blocks_mutex_);
        auto it = blocks_.find(owner_id);
        if (it == blocks_.end()) {
            continue;
        }
        size_t block_size = it->second.size;
        lock.unlock();

        if (demote(owner_id, lower_tier)) {
            freed += block_size;
            total_evictions_.fetch_add(1);
        }
    }

    return freed;
}

void TieredMemoryAllocator::set_eviction_policy(EvictionPolicy policy) {
    eviction_policy_.store(policy);
}

EvictionPolicy TieredMemoryAllocator::get_eviction_policy() const {
    return eviction_policy_.load();
}

std::vector<std::string> TieredMemoryAllocator::select_eviction_candidates(
    MemoryTier tier,
    size_t bytes_needed
) {
    std::shared_lock<std::shared_mutex> lock(blocks_mutex_);

    // Collect blocks in this tier
    std::vector<std::pair<std::string, MemoryBlock>> tier_blocks;
    for (const auto& [owner_id, block] : blocks_) {
        if (block.tier == tier) {
            tier_blocks.emplace_back(owner_id, block);
        }
    }

    if (tier_blocks.empty()) {
        return {};
    }

    // Sort based on policy
    EvictionPolicy policy = eviction_policy_.load();

    switch (policy) {
        case EvictionPolicy::LRU:
            std::sort(tier_blocks.begin(), tier_blocks.end(),
                [](const auto& a, const auto& b) {
                    return a.second.last_access < b.second.last_access;
                });
            break;

        case EvictionPolicy::LFU:
            std::sort(tier_blocks.begin(), tier_blocks.end(),
                [](const auto& a, const auto& b) {
                    return a.second.access_count < b.second.access_count;
                });
            break;

        case EvictionPolicy::FIFO:
            std::sort(tier_blocks.begin(), tier_blocks.end(),
                [](const auto& a, const auto& b) {
                    return a.second.created_at < b.second.created_at;
                });
            break;

        case EvictionPolicy::SIZE_WEIGHTED:
            // LRU but prefer larger items for eviction
            std::sort(tier_blocks.begin(), tier_blocks.end(),
                [](const auto& a, const auto& b) {
                    auto age_a = std::chrono::steady_clock::now() - a.second.last_access;
                    auto age_b = std::chrono::steady_clock::now() - b.second.last_access;
                    // Score = age * size (higher = better candidate)
                    return (age_a.count() * a.second.size) > (age_b.count() * b.second.size);
                });
            break;
    }

    // Select candidates until we have enough
    std::vector<std::string> candidates;
    size_t total = 0;

    for (const auto& [owner_id, block] : tier_blocks) {
        candidates.push_back(owner_id);
        total += block.size;
        if (total >= bytes_needed) {
            break;
        }
    }

    return candidates;
}

void TieredMemoryAllocator::notify_eviction(const std::string& owner_id, MemoryTier tier) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);

    for (const auto& [id, callback] : eviction_callbacks_) {
        try {
            callback(owner_id, tier);
        } catch (...) {
            // Ignore callback exceptions
        }
    }
}

//=============================================================================
// Query Operations
//=============================================================================

size_t TieredMemoryAllocator::available(MemoryTier tier) const {
    const TierStorage& storage = get_storage(tier);
    size_t used = storage.used.load();
    return used < storage.capacity ? storage.capacity - used : 0;
}

size_t TieredMemoryAllocator::used(MemoryTier tier) const {
    return get_storage(tier).used.load();
}

size_t TieredMemoryAllocator::capacity(MemoryTier tier) const {
    return get_storage(tier).capacity;
}

std::optional<MemoryBlock> TieredMemoryAllocator::get_block(const std::string& owner_id) const {
    std::shared_lock<std::shared_mutex> lock(blocks_mutex_);

    auto it = blocks_.find(owner_id);
    if (it == blocks_.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::vector<MemoryBlock> TieredMemoryAllocator::get_blocks_in_tier(MemoryTier tier) const {
    std::shared_lock<std::shared_mutex> lock(blocks_mutex_);

    std::vector<MemoryBlock> result;
    for (const auto& [owner_id, block] : blocks_) {
        if (block.tier == tier) {
            result.push_back(block);
        }
    }

    return result;
}

//=============================================================================
// Statistics
//=============================================================================

MemoryStats TieredMemoryAllocator::get_stats() const {
    MemoryStats stats;

    stats.gpu = get_tier_stats(MemoryTier::GPU_HBM);
    stats.cpu = get_tier_stats(MemoryTier::CPU_RAM);
    stats.ssd = get_tier_stats(MemoryTier::SSD_NVME);

    stats.total_allocations = gpu_storage_.allocations.load() +
                              cpu_storage_.allocations.load() +
                              ssd_storage_.allocations.load();

    stats.total_deallocations = gpu_storage_.deallocations.load() +
                                cpu_storage_.deallocations.load() +
                                ssd_storage_.deallocations.load();

    stats.total_promotions = gpu_storage_.promotions.load() +
                             cpu_storage_.promotions.load();

    stats.total_demotions = gpu_storage_.demotions.load() +
                            cpu_storage_.demotions.load() +
                            ssd_storage_.demotions.load();

    stats.total_evictions = total_evictions_.load();

    return stats;
}

TierStats TieredMemoryAllocator::get_tier_stats(MemoryTier tier) const {
    const TierStorage& storage = get_storage(tier);

    TierStats stats;
    stats.tier = tier;
    stats.capacity_bytes = storage.capacity;
    stats.used_bytes = storage.used.load();
    stats.available_bytes = stats.capacity_bytes > stats.used_bytes ?
                            stats.capacity_bytes - stats.used_bytes : 0;

    // Count items in tier
    {
        std::shared_lock<std::shared_mutex> lock(blocks_mutex_);
        for (const auto& [owner_id, block] : blocks_) {
            if (block.tier == tier) {
                stats.item_count++;
            }
        }
    }

    uint64_t accesses = storage.accesses.load();
    uint64_t hits = storage.hits.load();
    stats.hit_rate = accesses > 0 ? static_cast<double>(hits) / accesses : 0.0;

    stats.promotions = storage.promotions.load();
    stats.demotions = storage.demotions.load();

    return stats;
}

void TieredMemoryAllocator::reset_stats() {
    gpu_storage_.allocations.store(0);
    gpu_storage_.deallocations.store(0);
    gpu_storage_.promotions.store(0);
    gpu_storage_.demotions.store(0);
    gpu_storage_.hits.store(0);
    gpu_storage_.accesses.store(0);

    cpu_storage_.allocations.store(0);
    cpu_storage_.deallocations.store(0);
    cpu_storage_.promotions.store(0);
    cpu_storage_.demotions.store(0);
    cpu_storage_.hits.store(0);
    cpu_storage_.accesses.store(0);

    ssd_storage_.allocations.store(0);
    ssd_storage_.deallocations.store(0);
    ssd_storage_.promotions.store(0);
    ssd_storage_.demotions.store(0);
    ssd_storage_.hits.store(0);
    ssd_storage_.accesses.store(0);

    total_evictions_.store(0);
}

//=============================================================================
// Access Tracking
//=============================================================================

void TieredMemoryAllocator::record_access(const std::string& owner_id) {
    std::unique_lock<std::shared_mutex> lock(blocks_mutex_);

    auto it = blocks_.find(owner_id);
    if (it == blocks_.end()) {
        return;
    }

    MemoryBlock& block = it->second;
    block.access_count++;
    block.last_access = std::chrono::steady_clock::now();

    TierStorage& storage = get_storage(block.tier);
    storage.accesses.fetch_add(1);
    storage.hits.fetch_add(1);
}

//=============================================================================
// Observable Interface
//=============================================================================

uint64_t TieredMemoryAllocator::on_eviction(EvictionCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);

    uint64_t id = next_callback_id_.fetch_add(1);
    eviction_callbacks_[id] = std::move(callback);
    return id;
}

void TieredMemoryAllocator::remove_eviction_callback(uint64_t subscription_id) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    eviction_callbacks_.erase(subscription_id);
}

//=============================================================================
// Internal Methods
//=============================================================================

void* TieredMemoryAllocator::allocate_raw(size_t size, MemoryTier tier) {
    void* ptr = nullptr;

    switch (tier) {
        case MemoryTier::GPU_HBM:
#if CUDA_AVAILABLE
            if (cuda_available_) {
                if (cudaMalloc(&ptr, size) != cudaSuccess) {
                    ptr = nullptr;
                }
            }
#endif
            break;

        case MemoryTier::CPU_RAM:
#if CUDA_AVAILABLE
            if (config_.use_pinned_memory && cuda_available_) {
                if (cudaMallocHost(&ptr, size) != cudaSuccess) {
                    // Fall back to regular malloc
                    ptr = ALIGNED_ALLOC(config_.alignment, size);
                }
            } else
#endif
            {
                ptr = ALIGNED_ALLOC(config_.alignment, size);
            }
            break;

        case MemoryTier::SSD_NVME:
            // SSD tier uses file-based storage, not direct allocation
            break;
    }

    return ptr;
}

void TieredMemoryAllocator::free_raw(void* ptr, size_t size, MemoryTier tier) {
    if (!ptr) {
        return;
    }

    switch (tier) {
        case MemoryTier::GPU_HBM:
#if CUDA_AVAILABLE
            if (cuda_available_) {
                cudaFree(ptr);
            }
#endif
            break;

        case MemoryTier::CPU_RAM:
#if CUDA_AVAILABLE
            if (config_.use_pinned_memory && cuda_available_) {
                // Try pinned free first, fall back to regular free
                if (cudaFreeHost(ptr) != cudaSuccess) {
                    ALIGNED_FREE(ptr);
                }
            } else
#endif
            {
                ALIGNED_FREE(ptr);
            }
            break;

        case MemoryTier::SSD_NVME:
            // SSD tier uses file-based storage
            break;
    }

    (void)size;  // Unused but may be needed for tracking
}

bool TieredMemoryAllocator::copy_between_tiers(
    void* dst, MemoryTier dst_tier,
    const void* src, MemoryTier src_tier,
    size_t size
) {
#if CUDA_AVAILABLE
    if (cuda_available_) {
        int kind = cudaMemcpyHostToHost;

        if (src_tier == MemoryTier::GPU_HBM && dst_tier == MemoryTier::GPU_HBM) {
            kind = cudaMemcpyDeviceToDevice;
        } else if (src_tier == MemoryTier::GPU_HBM) {
            kind = cudaMemcpyDeviceToHost;
        } else if (dst_tier == MemoryTier::GPU_HBM) {
            kind = cudaMemcpyHostToDevice;
        }

        return cudaMemcpy(dst, src, size, kind) == cudaSuccess;
    }
#endif

    // CPU-only copy
    if (src_tier != MemoryTier::GPU_HBM && dst_tier != MemoryTier::GPU_HBM) {
        std::memcpy(dst, src, size);
        return true;
    }

    return false;
}

TieredMemoryAllocator::TierStorage& TieredMemoryAllocator::get_storage(MemoryTier tier) {
    switch (tier) {
        case MemoryTier::GPU_HBM: return gpu_storage_;
        case MemoryTier::CPU_RAM: return cpu_storage_;
        case MemoryTier::SSD_NVME: return ssd_storage_;
    }
    return cpu_storage_;  // Default
}

const TieredMemoryAllocator::TierStorage& TieredMemoryAllocator::get_storage(MemoryTier tier) const {
    switch (tier) {
        case MemoryTier::GPU_HBM: return gpu_storage_;
        case MemoryTier::CPU_RAM: return cpu_storage_;
        case MemoryTier::SSD_NVME: return ssd_storage_;
    }
    return cpu_storage_;  // Default
}

size_t TieredMemoryAllocator::align_size(size_t size) const {
    size_t alignment = config_.alignment;
    return (size + alignment - 1) & ~(alignment - 1);
}

} // namespace snapllm
