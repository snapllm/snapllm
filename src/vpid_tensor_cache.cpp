/**
 * @file vpid_tensor_cache.cpp
 * @brief vDPE Tensor Cache Implementation
 */

#include "snapllm/vpid_tensor_cache.h"
#include <iostream>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <unistd.h>
#endif

namespace snapllm {

VPIDTensorCache::VPIDTensorCache(size_t cache_budget_bytes)
    : budget_bytes_(cache_budget_bytes)
    , used_bytes_(0)
    , cache_hits_(0)
    , cache_misses_(0)
    , eviction_count_(0)
    , current_time_(0)
{
    std::cout << "[VPIDTensorCache] Initialized with "
              << (budget_bytes_ / (1024.0 * 1024 * 1024)) << " GB budget" << std::endl;
}

VPIDTensorCache::~VPIDTensorCache() {
    clear_all();

    // Print final statistics
    std::cout << "[VPIDTensorCache] Shutdown statistics:" << std::endl;
    std::cout << "  Total cache hits: " << cache_hits_ << std::endl;
    std::cout << "  Total cache misses: " << cache_misses_ << std::endl;
    std::cout << "  Cache hit rate: " << (get_hit_rate() * 100) << "%" << std::endl;
    std::cout << "  Total evictions: " << eviction_count_ << std::endl;
}

void* VPIDTensorCache::allocate_aligned_buffer(size_t size) {
#ifdef _WIN32
    // Windows: Use VirtualAlloc for page-aligned memory (required for Direct I/O)
    // Allocate with PAGE_READWRITE and MEM_COMMIT
    void* ptr = VirtualAlloc(
        NULL,
        size,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (ptr == NULL) {
        std::cerr << "[VPIDTensorCache] VirtualAlloc failed: " << GetLastError() << std::endl;
        return nullptr;
    }

    return ptr;
#else
    // Linux: Use posix_memalign for aligned memory
    void* ptr = nullptr;
    size_t alignment = 4096; // Page size
    if (posix_memalign(&ptr, alignment, size) != 0) {
        std::cerr << "[VPIDTensorCache] posix_memalign failed" << std::endl;
        return nullptr;
    }
    return ptr;
#endif
}

void VPIDTensorCache::free_aligned_buffer(void* ptr, size_t size) {
    if (!ptr) return;

#ifdef _WIN32
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    free(ptr);
#endif
}

void* VPIDTensorCache::allocate_slot(const std::string& name, size_t size) {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    // Check if already cached
    auto it = cache_.find(name);
    if (it != cache_.end()) {
        update_lru(name);
        return it->second.data_ptr;
    }

    // Evict until we have enough space
    if (used_bytes_ + size > budget_bytes_) {
        if (!evict_until_free(size)) {
            std::cerr << "[VPIDTensorCache] Failed to free enough space for tensor '"
                      << name << "' (" << (size / (1024.0 * 1024)) << " MB)" << std::endl;
            return nullptr;
        }
    }

    // Allocate aligned buffer
    void* ptr = allocate_aligned_buffer(size);
    if (!ptr) {
        std::cerr << "[VPIDTensorCache] Failed to allocate " << (size / (1024.0 * 1024))
                  << " MB for tensor '" << name << "'" << std::endl;
        return nullptr;
    }

    // Add to cache
    CachedTensor entry(name, ptr, size);
    entry.last_access_time = ++current_time_;
    entry.access_count = 1;

    cache_[name] = entry;
    used_bytes_ += size;

    // Add to LRU list
    lru_list_.push_back(name);
    lru_map_[name] = --lru_list_.end();

    cache_misses_++;

    std::cout << "[VPIDTensorCache] Allocated " << (size / (1024.0 * 1024))
              << " MB for '" << name << "' (cache: "
              << (used_bytes_ / (1024.0 * 1024)) << " MB / "
              << (budget_bytes_ / (1024.0 * 1024)) << " MB)" << std::endl;

    return ptr;
}

void* VPIDTensorCache::get_tensor(const std::string& name) {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    auto it = cache_.find(name);
    if (it != cache_.end()) {
        update_lru(name);
        cache_hits_++;
        it->second.access_count++;
        return it->second.data_ptr;
    }

    cache_misses_++;
    return nullptr;
}

bool VPIDTensorCache::is_cached(const std::string& name) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return cache_.find(name) != cache_.end();
}

void VPIDTensorCache::update_lru(const std::string& name) {
    // Move to end of LRU list (most recently used)
    auto lru_it = lru_map_.find(name);
    if (lru_it != lru_map_.end()) {
        lru_list_.erase(lru_it->second);
        lru_list_.push_back(name);
        lru_map_[name] = --lru_list_.end();
    }

    // Update timestamp
    auto cache_it = cache_.find(name);
    if (cache_it != cache_.end()) {
        cache_it->second.last_access_time = ++current_time_;
    }
}

bool VPIDTensorCache::evict_tensor(const std::string& name) {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    auto it = cache_.find(name);
    if (it == cache_.end()) {
        return false;
    }

    // Free buffer
    free_aligned_buffer(it->second.data_ptr, it->second.size);
    used_bytes_ -= it->second.size;

    std::cout << "[VPIDTensorCache] Evicted '" << name << "' ("
              << (it->second.size / (1024.0 * 1024)) << " MB)" << std::endl;

    // Remove from LRU tracking
    auto lru_it = lru_map_.find(name);
    if (lru_it != lru_map_.end()) {
        lru_list_.erase(lru_it->second);
        lru_map_.erase(lru_it);
    }

    // Remove from cache
    cache_.erase(it);
    eviction_count_++;

    return true;
}

size_t VPIDTensorCache::evict_lru() {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    if (lru_list_.empty()) {
        return 0;
    }

    // Get least recently used tensor (front of list)
    std::string lru_name = lru_list_.front();
    auto it = cache_.find(lru_name);
    if (it == cache_.end()) {
        // Inconsistency - remove from LRU list
        lru_list_.pop_front();
        lru_map_.erase(lru_name);
        return 0;
    }

    size_t freed = it->second.size;

    // Free buffer
    free_aligned_buffer(it->second.data_ptr, it->second.size);
    used_bytes_ -= it->second.size;

    std::cout << "[VPIDTensorCache] Evicted LRU '" << lru_name << "' ("
              << (freed / (1024.0 * 1024)) << " MB)" << std::endl;

    // Remove from tracking
    lru_list_.pop_front();
    lru_map_.erase(lru_name);
    cache_.erase(it);
    eviction_count_++;

    return freed;
}

bool VPIDTensorCache::evict_until_free(size_t required_bytes) {
    size_t available = budget_bytes_ - used_bytes_;

    while (available < required_bytes && !lru_list_.empty()) {
        size_t freed = evict_lru();
        if (freed == 0) break; // Failed to evict
        available += freed;
    }

    return available >= required_bytes;
}

void VPIDTensorCache::clear_all() {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    std::cout << "[VPIDTensorCache] Clearing all cached tensors ("
              << cache_.size() << " tensors, "
              << (used_bytes_ / (1024.0 * 1024)) << " MB)" << std::endl;

    // Free all buffers
    for (auto& pair : cache_) {
        free_aligned_buffer(pair.second.data_ptr, pair.second.size);
    }

    cache_.clear();
    lru_list_.clear();
    lru_map_.clear();
    used_bytes_ = 0;
}

std::vector<std::string> VPIDTensorCache::get_all_cached_names() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    std::vector<std::string> names;
    names.reserve(cache_.size());

    for (const auto& pair : cache_) {
        names.push_back(pair.first);
    }

    return names;
}

size_t VPIDTensorCache::get_tensor_size(const std::string& name) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    auto it = cache_.find(name);
    if (it != cache_.end()) {
        return it->second.size;
    }

    return 0;
}

} // namespace snapllm
