/**
 * @file prefetch_engine.h
 * @brief Statistical Prefetch Engine - Learn and predict access patterns
 */

#pragma once

#include "vpid_workspace.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace snapllm {

/**
 * @brief Prefetch Engine
 * 
 * Learns access patterns and predicts likely next tensors.
 *
 * The current workspace API exposes lookup by tensor name, but does not
 * expose the offset/size needed to load an uncached tensor. Consequently
 * prefetch() only accounts for cache residency; it never claims to have
 * loaded data that it could not load. Callers that own tensor metadata can
 * use VPIDWorkspace::read_direct() to perform an actual load.
 */
class PrefetchEngine {
public:
    explicit PrefetchEngine(std::shared_ptr<VPIDWorkspace> vpid);
    
    // Learning
    void record_access(const std::string& tensor_name);
    void record_pattern(const std::vector<std::string>& sequence);
    
    // Prediction
    std::vector<std::string> predict_next(const std::string& current);
    
    // Prefetching
    void prefetch(const std::vector<std::string>& tensors);
    
    // Statistics
    double get_hit_rate() const;
    void reset_stats();
    
private:
    std::shared_ptr<VPIDWorkspace> vpid_;
    
    // Access history and transition counts. All state is protected because
    // inference requests may record accesses concurrently.
    mutable std::mutex mutex_;
    std::string last_access_;
    std::unordered_map<std::string, std::unordered_map<std::string, uint64_t>> transitions_;
    uint64_t cache_hits_ = 0;
    uint64_t cache_misses_ = 0;
};

} // namespace snapllm
