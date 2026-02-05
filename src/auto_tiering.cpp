/**
 * @file auto_tiering.cpp
 * @brief Auto Tiering Implementation
 */

#include "snapllm/auto_tiering.h"
#include "snapllm/context_manager.h"
#include <iostream>
#include <algorithm>

namespace snapllm {

//=============================================================================
// Constructor / Destructor
//=============================================================================

AutoTieringManager::AutoTieringManager(
    ContextManager* context_manager,
    IMemoryAllocator* allocator,
    const AutoTieringConfig& config
)
    : context_manager_(context_manager)
    , allocator_(allocator)
    , config_(config)
    , last_check_(std::chrono::steady_clock::now())
{
    std::cout << "[AutoTiering] Initialized with policy: ";
    switch (config_.policy) {
        case TieringPolicy::ACCESS_FREQUENCY:
            std::cout << "ACCESS_FREQUENCY" << std::endl;
            break;
        case TieringPolicy::RECENCY:
            std::cout << "RECENCY" << std::endl;
            break;
        case TieringPolicy::ADAPTIVE:
            std::cout << "ADAPTIVE" << std::endl;
            break;
    }
}

AutoTieringManager::~AutoTieringManager() {
    stop();
}

//=============================================================================
// Lifecycle
//=============================================================================

void AutoTieringManager::start() {
    if (running_.load()) {
        return;
    }

    running_.store(true);
    worker_thread_ = std::make_unique<std::thread>(&AutoTieringManager::worker_loop, this);

    std::cout << "[AutoTiering] Background tiering started, checking every "
              << config_.check_interval.count() << " seconds" << std::endl;
}

void AutoTieringManager::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);
    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }
    worker_thread_.reset();

    std::cout << "[AutoTiering] Background tiering stopped" << std::endl;
}

//=============================================================================
// Access Recording
//=============================================================================

void AutoTieringManager::record_access(
    const std::string& context_id,
    size_t memory_bytes,
    MemoryTier tier
) {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    auto it = access_stats_.find(context_id);
    if (it == access_stats_.end()) {
        // New context
        ContextAccessStats stats;
        stats.context_id = context_id;
        stats.total_accesses = 1;
        stats.window_accesses = 1;
        stats.last_access = std::chrono::steady_clock::now();
        stats.created_at = std::chrono::steady_clock::now();
        stats.current_tier = tier;
        stats.memory_bytes = memory_bytes;
        access_stats_[context_id] = stats;
    } else {
        // Update existing
        it->second.total_accesses++;
        it->second.window_accesses++;
        it->second.last_access = std::chrono::steady_clock::now();
        if (memory_bytes > 0) {
            it->second.memory_bytes = memory_bytes;
        }
        it->second.current_tier = tier;
    }
}

void AutoTieringManager::remove_context(const std::string& context_id) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    access_stats_.erase(context_id);
}

//=============================================================================
// Manual Tiering
//=============================================================================

std::vector<TieringDecision> AutoTieringManager::check_now() {
    auto decisions = evaluate_tiering();
    apply_decisions(decisions);
    last_check_ = std::chrono::steady_clock::now();
    return decisions;
}

MemoryTier AutoTieringManager::get_recommended_tier(const std::string& context_id) const {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    auto it = access_stats_.find(context_id);
    if (it == access_stats_.end()) {
        return MemoryTier::CPU_RAM;  // Default to warm
    }

    const auto& stats = it->second;
    auto time_since = stats.time_since_access();

    switch (config_.policy) {
        case TieringPolicy::ACCESS_FREQUENCY:
            if (stats.window_accesses >= config_.hot_access_count) {
                return MemoryTier::GPU_HBM;
            } else if (stats.window_accesses >= config_.warm_access_count) {
                return MemoryTier::CPU_RAM;
            }
            return MemoryTier::SSD_NVME;

        case TieringPolicy::RECENCY:
            if (time_since < config_.hot_threshold) {
                return MemoryTier::GPU_HBM;
            } else if (time_since < config_.warm_threshold) {
                return MemoryTier::CPU_RAM;
            }
            return MemoryTier::SSD_NVME;

        case TieringPolicy::ADAPTIVE:
        default:
            // Combine frequency and recency
            bool recent = time_since < config_.hot_threshold;
            bool frequent = stats.window_accesses >= config_.hot_access_count;

            if (recent && frequent) {
                return MemoryTier::GPU_HBM;
            } else if (recent || stats.window_accesses >= config_.warm_access_count) {
                return MemoryTier::CPU_RAM;
            } else if (time_since > config_.cold_threshold) {
                return MemoryTier::SSD_NVME;
            }
            return stats.current_tier;  // Keep current tier
    }
}

//=============================================================================
// Statistics
//=============================================================================

std::optional<ContextAccessStats> AutoTieringManager::get_stats(const std::string& context_id) const {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    auto it = access_stats_.find(context_id);
    if (it == access_stats_.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::vector<ContextAccessStats> AutoTieringManager::get_all_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    std::vector<ContextAccessStats> result;
    result.reserve(access_stats_.size());

    for (const auto& [id, stats] : access_stats_) {
        result.push_back(stats);
    }

    return result;
}

AutoTieringManager::Summary AutoTieringManager::get_summary() const {
    Summary summary;

    std::lock_guard<std::mutex> lock(stats_mutex_);

    summary.total_contexts = access_stats_.size();

    for (const auto& [id, stats] : access_stats_) {
        switch (stats.current_tier) {
            case MemoryTier::GPU_HBM:
                summary.hot_contexts++;
                break;
            case MemoryTier::CPU_RAM:
                summary.warm_contexts++;
                break;
            case MemoryTier::SSD_NVME:
                summary.cold_contexts++;
                break;
        }
    }

    summary.total_promotions = total_promotions_.load();
    summary.total_demotions = total_demotions_.load();
    summary.emergency_demotions = emergency_demotions_.load();
    summary.last_check = last_check_;

    return summary;
}

//=============================================================================
// Configuration
//=============================================================================

void AutoTieringManager::set_config(const AutoTieringConfig& config) {
    config_ = config;
    std::cout << "[AutoTiering] Configuration updated" << std::endl;
}

uint64_t AutoTieringManager::on_tiering_decision(TieringCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);

    uint64_t id = next_callback_id_.fetch_add(1);
    callbacks_[id] = std::move(callback);
    return id;
}

void AutoTieringManager::remove_callback(uint64_t subscription_id) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    callbacks_.erase(subscription_id);
}

//=============================================================================
// Internal Methods
//=============================================================================

void AutoTieringManager::worker_loop() {
    while (running_.load()) {
        // Sleep for check interval
        auto sleep_time = config_.check_interval;
        auto sleep_ms = std::chrono::duration_cast<std::chrono::milliseconds>(sleep_time);

        // Sleep in small increments to allow quick shutdown
        for (int i = 0; i < sleep_ms.count() / 100 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!running_.load()) {
            break;
        }

        // Check for memory pressure first
        bool gpu_pressure = check_memory_pressure(MemoryTier::GPU_HBM);
        bool cpu_pressure = check_memory_pressure(MemoryTier::CPU_RAM);

        std::vector<TieringDecision> decisions;

        if (gpu_pressure) {
            auto emergency = handle_memory_pressure(MemoryTier::GPU_HBM);
            decisions.insert(decisions.end(), emergency.begin(), emergency.end());
        }

        if (cpu_pressure) {
            auto emergency = handle_memory_pressure(MemoryTier::CPU_RAM);
            decisions.insert(decisions.end(), emergency.begin(), emergency.end());
        }

        // Regular tiering evaluation
        auto regular = evaluate_tiering();
        decisions.insert(decisions.end(), regular.begin(), regular.end());

        // Apply decisions
        if (!decisions.empty()) {
            apply_decisions(decisions);
        }

        // Reset window counts periodically
        reset_window_counts();

        last_check_ = std::chrono::steady_clock::now();
    }
}

std::vector<TieringDecision> AutoTieringManager::evaluate_tiering() {
    std::vector<TieringDecision> decisions;

    std::lock_guard<std::mutex> lock(stats_mutex_);

    for (auto& [context_id, stats] : access_stats_) {
        MemoryTier recommended = get_recommended_tier(context_id);

        if (recommended != stats.current_tier) {
            TieringDecision decision;
            decision.context_id = context_id;
            decision.current_tier = stats.current_tier;
            decision.target_tier = recommended;

            if (decision.is_promotion()) {
                decision.reason = "High access frequency/recency";
            } else {
                decision.reason = "Low access frequency/recency";
            }

            decisions.push_back(decision);
        }
    }

    return decisions;
}

void AutoTieringManager::apply_decisions(const std::vector<TieringDecision>& decisions) {
    for (const auto& decision : decisions) {
        bool success = false;

        if (decision.is_promotion()) {
            // Use allocator to promote
            if (allocator_) {
                success = allocator_->promote(decision.context_id, decision.target_tier);
            }

            // Also notify context manager
            if (context_manager_ && success) {
                std::string tier_name = "warm";
                if (decision.target_tier == MemoryTier::GPU_HBM) {
                    tier_name = "hot";
                }
                // context_manager_->promote() takes string tier name
            }

            if (success) {
                total_promotions_.fetch_add(1);
                std::cout << "[AutoTiering] Promoted '" << decision.context_id
                          << "' to " << memory_tier_to_string(decision.target_tier)
                          << " - " << decision.reason << std::endl;
            }
        } else if (decision.is_demotion()) {
            // Use allocator to demote
            if (allocator_) {
                success = allocator_->demote(decision.context_id, decision.target_tier);
            }

            if (success) {
                total_demotions_.fetch_add(1);
                std::cout << "[AutoTiering] Demoted '" << decision.context_id
                          << "' to " << memory_tier_to_string(decision.target_tier)
                          << " - " << decision.reason << std::endl;
            }
        }

        // Update local stats
        if (success) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            auto it = access_stats_.find(decision.context_id);
            if (it != access_stats_.end()) {
                it->second.current_tier = decision.target_tier;
            }
        }

        // Notify callbacks
        notify_callbacks(decision);
    }
}

bool AutoTieringManager::check_memory_pressure(MemoryTier tier) {
    if (!allocator_) {
        return false;
    }

    size_t used = allocator_->used(tier);
    size_t capacity = allocator_->capacity(tier);

    if (capacity == 0) {
        return false;
    }

    double utilization = static_cast<double>(used) / capacity;

    double threshold = tier == MemoryTier::GPU_HBM ?
        config_.gpu_pressure_threshold : config_.cpu_pressure_threshold;

    return utilization > threshold;
}

std::vector<TieringDecision> AutoTieringManager::handle_memory_pressure(MemoryTier tier) {
    std::vector<TieringDecision> decisions;

    if (!allocator_) {
        return decisions;
    }

    // Calculate how much to free
    size_t used = allocator_->used(tier);
    size_t capacity = allocator_->capacity(tier);
    size_t target = static_cast<size_t>(capacity * config_.target_utilization);
    size_t to_free = used > target ? used - target : 0;

    if (to_free == 0) {
        return decisions;
    }

    std::cout << "[AutoTiering] Memory pressure on " << memory_tier_to_string(tier)
              << ", need to free " << (to_free / (1024 * 1024)) << " MB" << std::endl;

    // Get blocks in this tier, sorted by access time (oldest first)
    std::vector<ContextAccessStats> tier_contexts;

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        for (const auto& [id, stats] : access_stats_) {
            if (stats.current_tier == tier) {
                tier_contexts.push_back(stats);
            }
        }
    }

    // Sort by last access (oldest first)
    std::sort(tier_contexts.begin(), tier_contexts.end(),
        [](const auto& a, const auto& b) {
            return a.last_access < b.last_access;
        });

    // Select contexts to demote
    size_t freed = 0;
    MemoryTier target_tier = tier == MemoryTier::GPU_HBM ?
        MemoryTier::CPU_RAM : MemoryTier::SSD_NVME;

    for (const auto& stats : tier_contexts) {
        if (freed >= to_free) {
            break;
        }

        TieringDecision decision;
        decision.context_id = stats.context_id;
        decision.current_tier = tier;
        decision.target_tier = target_tier;
        decision.reason = "Memory pressure emergency demotion";

        decisions.push_back(decision);
        freed += stats.memory_bytes;
        emergency_demotions_.fetch_add(1);
    }

    return decisions;
}

void AutoTieringManager::notify_callbacks(const TieringDecision& decision) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);

    for (const auto& [id, callback] : callbacks_) {
        try {
            callback(decision);
        } catch (...) {
            // Ignore callback exceptions
        }
    }
}

void AutoTieringManager::reset_window_counts() {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    for (auto& [id, stats] : access_stats_) {
        stats.window_accesses = 0;
    }
}

} // namespace snapllm
