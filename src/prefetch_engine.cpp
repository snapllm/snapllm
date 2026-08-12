/**
 * @file prefetch_engine.cpp
 * @brief Prefetch Engine Implementation
 */

#include "snapllm/prefetch_engine.h"

#include <algorithm>
#include <utility>

namespace snapllm {

PrefetchEngine::PrefetchEngine(std::shared_ptr<VPIDWorkspace> vpid)
    : vpid_(vpid)
{
}

void PrefetchEngine::record_access(const std::string& tensor_name) {
    if (tensor_name.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!last_access_.empty()) ++transitions_[last_access_][tensor_name];
    last_access_ = tensor_name;
    if (vpid_ && vpid_->get_cached_tensor(tensor_name)) ++cache_hits_;
    else ++cache_misses_;
}

void PrefetchEngine::record_pattern(const std::vector<std::string>& sequence) {
    if (sequence.size() < 2) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 1; i < sequence.size(); ++i) {
        if (!sequence[i - 1].empty() && !sequence[i].empty())
            ++transitions_[sequence[i - 1]][sequence[i]];
    }
}

std::vector<std::string> PrefetchEngine::predict_next(const std::string& current) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = transitions_.find(current);
    if (it == transitions_.end()) return {};
    std::vector<std::pair<std::string, uint64_t>> ranked(it->second.begin(), it->second.end());
    std::sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second != rhs.second) return lhs.second > rhs.second;
        return lhs.first < rhs.first;
    });
    std::vector<std::string> result;
    result.reserve(ranked.size());
    for (const auto& entry : ranked) result.push_back(entry.first);
    return result;
}

void PrefetchEngine::prefetch(const std::vector<std::string>& tensors) {
    // Tensor offsets/sizes are not available here, so account for residency
    // without pretending that uncached tensors were loaded.
    for (const auto& tensor : tensors) {
        record_access(tensor);
    }
}

double PrefetchEngine::get_hit_rate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t total = cache_hits_ + cache_misses_;
    return total == 0 ? 0.0 : static_cast<double>(cache_hits_) / static_cast<double>(total);
}

void PrefetchEngine::reset_stats() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_hits_ = 0;
    cache_misses_ = 0;
}

} // namespace snapllm
