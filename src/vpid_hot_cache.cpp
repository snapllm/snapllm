/**
 * @file vpid_hot_cache.cpp
 * @brief Implementation of HOT tier RAM cache
 */

#include "snapllm/vpid_hot_cache.h"
#include <iostream>
#include <algorithm>
#include <cstring>

namespace snapllm {

VPIDHotCache::VPIDHotCache(size_t max_size_bytes)
    : max_size_bytes_(max_size_bytes)
{
    std::cout << "VPIDHotCache: Initialized with "
              << (max_size_bytes / (1024.0 * 1024.0 * 1024.0))
              << " GB budget" << std::endl;
}

const float* VPIDHotCache::get_or_load(
    const std::string& model_name,
    const std::string& tensor_name,
    const float* disk_ptr,
    size_t count)
{
    std::string key = make_key(model_name, tensor_name);

    // Check if in cache (HOT tier) - NO LOCK for read-only check
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // HIT: Return from RAM cache
        stats_.hits++;

        HotCacheEntry& entry = it->second;
        entry.access_count++;
        entry.last_access_time = access_clock_++;

        return entry.data.data();
    }

    // MISS: Tensor not in cache
    stats_.misses++;
    return disk_ptr;  // Don't load on-demand, only via prefetch
}

const float* VPIDHotCache::get_if_cached(
    const std::string& model_name,
    const std::string& tensor_name)
{
    std::string key = make_key(model_name, tensor_name);

    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second.data.data();
    }

    return nullptr;
}

bool VPIDHotCache::prefetch(
    const std::string& model_name,
    const std::string& tensor_name,
    const float* disk_ptr,
    size_t count)
{
    if (disk_ptr == nullptr) return false;

    std::string key = make_key(model_name, tensor_name);

    std::lock_guard<std::mutex> lock(cache_mutex_);

    // Already cached?
    if (cache_.find(key) != cache_.end()) {
        return true;
    }

    // Load into cache
    return load_into_cache(key, model_name, tensor_name, disk_ptr, count);
}

void VPIDHotCache::evict_model(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    std::vector<std::string> keys_to_remove;

    // Find all entries for this model
    for (const auto& [key, entry] : cache_) {
        if (entry.model_name == model_name) {
            keys_to_remove.push_back(key);
        }
    }

    // Remove entries
    for (const std::string& key : keys_to_remove) {
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            // Update stats
            stats_.current_size_bytes -= it->second.tensor_size_bytes;
            stats_.current_entries--;

            // Remove from LRU
            auto lru_it = lru_map_.find(key);
            if (lru_it != lru_map_.end()) {
                lru_list_.erase(lru_it->second);
                lru_map_.erase(lru_it);
            }

            // Remove from cache
            cache_.erase(it);
        }
    }

    std::cout << "VPIDHotCache: Evicted " << keys_to_remove.size()
              << " tensors for model '" << model_name << "'" << std::endl;
}

void VPIDHotCache::clear() {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    cache_.clear();
    lru_list_.clear();
    lru_map_.clear();

    stats_.current_size_bytes = 0;
    stats_.current_entries = 0;

    std::cout << "VPIDHotCache: Cleared all entries" << std::endl;
}

void VPIDHotCache::touch(const std::string& key) {
    // Update LRU: move to front
    auto lru_it = lru_map_.find(key);
    if (lru_it != lru_map_.end()) {
        lru_list_.erase(lru_it->second);
    }

    lru_list_.push_front(key);
    lru_map_[key] = lru_list_.begin();
}

void VPIDHotCache::evict_lru() {
    if (lru_list_.empty()) return;

    // Get least recently used entry
    std::string lru_key = lru_list_.back();
    lru_list_.pop_back();
    lru_map_.erase(lru_key);

    // Remove from cache
    auto it = cache_.find(lru_key);
    if (it != cache_.end()) {
        size_t freed_bytes = it->second.tensor_size_bytes;

        cache_.erase(it);

        // Update stats
        stats_.current_size_bytes -= freed_bytes;
        stats_.current_entries--;
        stats_.evictions++;
    }
}

bool VPIDHotCache::make_space(size_t needed_bytes) {
    // Evict until we have enough space
    while (stats_.current_size_bytes.load() + needed_bytes > max_size_bytes_) {
        if (lru_list_.empty()) {
            // Cache empty, can't make more space
            return false;
        }

        evict_lru();
    }

    return true;
}

bool VPIDHotCache::load_into_cache(
    const std::string& key,
    const std::string& model_name,
    const std::string& tensor_name,
    const float* disk_ptr,
    size_t count)
{
    size_t tensor_bytes = count * sizeof(float);

    // Check if tensor is too large for cache
    if (tensor_bytes > max_size_bytes_) {
        // Tensor larger than entire cache budget, don't cache
        return false;
    }

    // Make space if needed
    if (!make_space(tensor_bytes)) {
        return false;
    }

    // Create cache entry
    HotCacheEntry entry;
    entry.model_name = model_name;
    entry.tensor_name = tensor_name;
    entry.tensor_size_bytes = tensor_bytes;
    entry.access_count = 1;
    entry.last_access_time = access_clock_++;

    // Copy data from disk to RAM
    entry.data.resize(count);
    std::memcpy(entry.data.data(), disk_ptr, tensor_bytes);

    // Insert into cache
    cache_[key] = std::move(entry);

    // Update LRU
    touch(key);

    // Update stats
    stats_.current_size_bytes += tensor_bytes;
    stats_.current_entries++;
    stats_.loads++;

    return true;
}

} // namespace snapllm
