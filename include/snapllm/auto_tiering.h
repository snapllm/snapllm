/**
 * @file auto_tiering.h
 * @brief Automatic Tiering Policy for vPID L2
 *
 * Implements automatic tier promotion/demotion based on access patterns:
 * - Frequently accessed contexts promoted to hot tier
 * - Idle contexts demoted to warm/cold tiers
 * - Memory pressure triggers emergency demotion
 *
 * Policies:
 * - ACCESS_FREQUENCY: Promote based on access count in time window
 * - RECENCY: Promote recently accessed, demote old
 * - ADAPTIVE: Combines frequency and recency with memory pressure
 */

#pragma once

#include "interfaces/i_memory_allocator.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

namespace snapllm {

// Forward declarations
class ContextManager;

/**
 * @brief Tiering policy type
 */
enum class TieringPolicy {
    ACCESS_FREQUENCY,   ///< Based on access count in time window
    RECENCY,           ///< Based on time since last access
    ADAPTIVE           ///< Combines both with memory pressure awareness
};

/**
 * @brief Configuration for auto-tiering
 */
struct AutoTieringConfig {
    TieringPolicy policy = TieringPolicy::ADAPTIVE;

    // Time windows
    std::chrono::seconds check_interval{60};        ///< How often to check for tiering
    std::chrono::seconds hot_threshold{300};        ///< Promote to hot if accessed within this window
    std::chrono::seconds warm_threshold{3600};      ///< Keep warm if accessed within this window
    std::chrono::seconds cold_threshold{86400};     ///< Demote to cold if not accessed in this time

    // Access frequency thresholds
    uint32_t hot_access_count = 10;     ///< Promote to hot if accessed this many times
    uint32_t warm_access_count = 3;     ///< Keep warm if accessed this many times

    // Memory thresholds
    double gpu_pressure_threshold = 0.85;   ///< Start demoting from GPU at this utilization
    double cpu_pressure_threshold = 0.90;   ///< Start demoting from CPU at this utilization
    double target_utilization = 0.70;       ///< Target utilization after emergency demotion

    // Limits
    size_t max_hot_contexts = 10;       ///< Maximum contexts in hot tier
    size_t max_warm_contexts = 50;      ///< Maximum contexts in warm tier

    static AutoTieringConfig defaults() {
        return AutoTieringConfig{};
    }

    static AutoTieringConfig aggressive() {
        AutoTieringConfig config;
        config.check_interval = std::chrono::seconds(30);
        config.hot_threshold = std::chrono::seconds(120);
        config.warm_threshold = std::chrono::seconds(600);
        config.gpu_pressure_threshold = 0.75;
        config.cpu_pressure_threshold = 0.85;
        return config;
    }

    static AutoTieringConfig conservative() {
        AutoTieringConfig config;
        config.check_interval = std::chrono::seconds(120);
        config.hot_threshold = std::chrono::seconds(600);
        config.warm_threshold = std::chrono::seconds(7200);
        config.gpu_pressure_threshold = 0.95;
        config.cpu_pressure_threshold = 0.95;
        return config;
    }
};

/**
 * @brief Access statistics for a context
 */
struct ContextAccessStats {
    std::string context_id;
    uint64_t total_accesses = 0;
    uint64_t window_accesses = 0;        ///< Accesses in current window
    std::chrono::steady_clock::time_point last_access;
    std::chrono::steady_clock::time_point created_at;
    MemoryTier current_tier = MemoryTier::CPU_RAM;
    size_t memory_bytes = 0;

    double access_rate() const {
        auto age = std::chrono::steady_clock::now() - created_at;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(age).count();
        return seconds > 0 ? static_cast<double>(total_accesses) / seconds : 0.0;
    }

    std::chrono::seconds time_since_access() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - last_access);
    }
};

/**
 * @brief Tiering decision
 */
struct TieringDecision {
    std::string context_id;
    MemoryTier current_tier;
    MemoryTier target_tier;
    std::string reason;

    bool is_promotion() const {
        return static_cast<int>(target_tier) > static_cast<int>(current_tier);
    }

    bool is_demotion() const {
        return static_cast<int>(target_tier) < static_cast<int>(current_tier);
    }
};

/**
 * @brief Callback for tiering decisions
 */
using TieringCallback = std::function<void(const TieringDecision& decision)>;

/**
 * @brief Auto Tiering Manager
 *
 * Monitors context access patterns and automatically manages tier placement.
 *
 * Usage:
 * @code
 * AutoTieringConfig config;
 * AutoTieringManager tiering(&context_manager, &allocator, config);
 *
 * // Start background monitoring
 * tiering.start();
 *
 * // Record access (call from ContextManager)
 * tiering.record_access("ctx_123");
 *
 * // Stop when done
 * tiering.stop();
 * @endcode
 */
class AutoTieringManager {
public:
    /**
     * @brief Construct auto-tiering manager
     * @param context_manager Pointer to context manager
     * @param allocator Pointer to memory allocator
     * @param config Tiering configuration
     */
    AutoTieringManager(
        ContextManager* context_manager,
        IMemoryAllocator* allocator,
        const AutoTieringConfig& config = AutoTieringConfig::defaults()
    );

    ~AutoTieringManager();

    // Non-copyable
    AutoTieringManager(const AutoTieringManager&) = delete;
    AutoTieringManager& operator=(const AutoTieringManager&) = delete;

    //=========================================================================
    // Lifecycle
    //=========================================================================

    /**
     * @brief Start background tiering thread
     */
    void start();

    /**
     * @brief Stop background tiering thread
     */
    void stop();

    /**
     * @brief Check if running
     */
    bool is_running() const { return running_.load(); }

    //=========================================================================
    // Access Recording
    //=========================================================================

    /**
     * @brief Record access to context
     * @param context_id Context identifier
     * @param memory_bytes Size of context in bytes
     * @param tier Current tier of context
     */
    void record_access(
        const std::string& context_id,
        size_t memory_bytes = 0,
        MemoryTier tier = MemoryTier::CPU_RAM
    );

    /**
     * @brief Remove context from tracking
     * @param context_id Context identifier
     */
    void remove_context(const std::string& context_id);

    //=========================================================================
    // Manual Tiering
    //=========================================================================

    /**
     * @brief Run tiering check immediately
     * @return Vector of tiering decisions made
     */
    std::vector<TieringDecision> check_now();

    /**
     * @brief Get recommended tier for context
     * @param context_id Context identifier
     * @return Recommended tier
     */
    MemoryTier get_recommended_tier(const std::string& context_id) const;

    //=========================================================================
    // Statistics
    //=========================================================================

    /**
     * @brief Get access statistics for context
     * @param context_id Context identifier
     * @return Statistics if found
     */
    std::optional<ContextAccessStats> get_stats(const std::string& context_id) const;

    /**
     * @brief Get all context statistics
     */
    std::vector<ContextAccessStats> get_all_stats() const;

    /**
     * @brief Get tiering summary
     */
    struct Summary {
        size_t total_contexts = 0;
        size_t hot_contexts = 0;
        size_t warm_contexts = 0;
        size_t cold_contexts = 0;

        uint64_t total_promotions = 0;
        uint64_t total_demotions = 0;
        uint64_t emergency_demotions = 0;

        std::chrono::steady_clock::time_point last_check;
    };

    Summary get_summary() const;

    //=========================================================================
    // Configuration
    //=========================================================================

    /**
     * @brief Update configuration
     * @param config New configuration
     */
    void set_config(const AutoTieringConfig& config);

    /**
     * @brief Get current configuration
     */
    const AutoTieringConfig& get_config() const { return config_; }

    /**
     * @brief Register callback for tiering decisions
     * @param callback Callback function
     * @return Subscription ID
     */
    uint64_t on_tiering_decision(TieringCallback callback);

    /**
     * @brief Remove callback
     * @param subscription_id Subscription ID
     */
    void remove_callback(uint64_t subscription_id);

private:
    ContextManager* context_manager_;
    IMemoryAllocator* allocator_;
    AutoTieringConfig config_;

    // Background thread
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> worker_thread_;

    // Access tracking
    mutable std::mutex stats_mutex_;
    std::unordered_map<std::string, ContextAccessStats> access_stats_;

    // Callbacks
    std::mutex callbacks_mutex_;
    std::unordered_map<uint64_t, TieringCallback> callbacks_;
    std::atomic<uint64_t> next_callback_id_{0};

    // Statistics
    std::atomic<uint64_t> total_promotions_{0};
    std::atomic<uint64_t> total_demotions_{0};
    std::atomic<uint64_t> emergency_demotions_{0};
    std::chrono::steady_clock::time_point last_check_;

    //=========================================================================
    // Internal Methods
    //=========================================================================

    /**
     * @brief Background worker function
     */
    void worker_loop();

    /**
     * @brief Evaluate tiering decisions
     */
    std::vector<TieringDecision> evaluate_tiering();

    /**
     * @brief Apply tiering decisions
     */
    void apply_decisions(const std::vector<TieringDecision>& decisions);

    /**
     * @brief Check for memory pressure
     */
    bool check_memory_pressure(MemoryTier tier);

    /**
     * @brief Handle emergency demotion due to memory pressure
     */
    std::vector<TieringDecision> handle_memory_pressure(MemoryTier tier);

    /**
     * @brief Notify callbacks
     */
    void notify_callbacks(const TieringDecision& decision);

    /**
     * @brief Reset window access counts
     */
    void reset_window_counts();
};

} // namespace snapllm
