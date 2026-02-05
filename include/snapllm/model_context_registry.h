/**
 * @file model_context_registry.h
 * @brief Model-Context Auto-Association Registry
 *
 * Provides automatic context discovery per model:
 * - Scans disk for contexts belonging to each model
 * - Auto-registers contexts when model loads
 * - Persists associations across unload/reload cycles
 * - No manual context specification needed
 *
 * Design:
 * - Maintains index: model_id → [context_ids]
 * - Index persisted to disk (survives restarts)
 * - Lazy loading: contexts stay on disk until accessed
 */

#pragma once

#include "workspace_paths.h"
#include "kv_cache.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <optional>
#include <functional>
#include <chrono>

namespace snapllm {

// Forward declarations
class ContextManager;
class FileCacheStore;

//=============================================================================
// Context Discovery Result
//=============================================================================

/**
 * @brief Information about a discovered context
 */
struct DiscoveredContext {
    std::string context_id;
    std::string model_id;
    std::string name;
    std::string source;           ///< Original source (file path, etc.)

    // Size info
    uint32_t token_count = 0;
    size_t storage_size_bytes = 0;
    bool is_compressed = false;

    // State
    std::string tier;             ///< "hot", "warm", "cold"
    bool is_loaded = false;       ///< Currently in memory?

    // Timestamps
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_accessed;

    // Validity
    bool is_valid = true;         ///< File exists and parseable
    std::string error_message;    ///< If not valid
};

/**
 * @brief Result of model context discovery
 */
struct ModelContextDiscovery {
    std::string model_id;
    std::vector<DiscoveredContext> contexts;

    // Summary
    size_t total_contexts = 0;
    size_t loaded_contexts = 0;
    size_t total_storage_bytes = 0;
    size_t total_tokens = 0;

    // Discovery metadata
    double scan_time_ms = 0.0;
    bool from_cache = false;      ///< Used cached index vs full scan
};

//=============================================================================
// Registry Index Entry
//=============================================================================

/**
 * @brief Persisted index entry for a context
 */
struct ContextIndexEntry {
    std::string context_id;
    std::string model_id;
    std::string name;
    std::string file_path;        ///< Path to cached KV data

    uint32_t token_count = 0;
    size_t storage_size_bytes = 0;

    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_accessed;

    // For quick validation without reading full file
    std::string content_hash;
    uint64_t file_size = 0;
    uint64_t file_mtime = 0;
};

//=============================================================================
// Model-Context Registry
//=============================================================================

/**
 * @brief Registry for automatic model-context association
 *
 * Usage:
 * @code
 * ModelContextRegistry registry(workspace_paths, &context_manager);
 *
 * // On model load - auto-discover its contexts
 * auto discovery = registry.discover_contexts("medicine");
 * // Returns all contexts belonging to medicine model
 *
 * // On startup - rebuild index from disk
 * registry.rebuild_index();
 *
 * // Quick lookup (uses cached index)
 * auto ctx_ids = registry.get_context_ids("medicine");
 * @endcode
 */
class ModelContextRegistry {
public:
    /**
     * @brief Construct registry
     * @param paths Workspace paths (for index storage)
     * @param context_manager Optional context manager for status queries
     */
    explicit ModelContextRegistry(
        const WorkspacePaths& paths,
        ContextManager* context_manager = nullptr
    );

    ~ModelContextRegistry();

    // Non-copyable
    ModelContextRegistry(const ModelContextRegistry&) = delete;
    ModelContextRegistry& operator=(const ModelContextRegistry&) = delete;

    //=========================================================================
    // Discovery Operations
    //=========================================================================

    /**
     * @brief Discover all contexts for a model
     * @param model_id Model identifier
     * @param force_scan Force disk scan (ignore cached index)
     * @return Discovery result with all contexts
     *
     * This is the main entry point. Call when:
     * - Model is loaded
     * - User requests context list
     * - Frontend needs to populate context dropdown
     */
    ModelContextDiscovery discover_contexts(
        const std::string& model_id,
        bool force_scan = false
    );

    /**
     * @brief Get context IDs for a model (fast, uses index)
     * @param model_id Model identifier
     * @return Vector of context IDs
     */
    std::vector<std::string> get_context_ids(const std::string& model_id) const;

    /**
     * @brief Get all models that have contexts
     * @return Vector of model IDs
     */
    std::vector<std::string> get_models_with_contexts() const;

    /**
     * @brief Check if model has any contexts
     * @param model_id Model identifier
     * @return true if at least one context exists
     */
    bool has_contexts(const std::string& model_id) const;

    /**
     * @brief Get context count for a model
     * @param model_id Model identifier
     * @return Number of contexts
     */
    size_t context_count(const std::string& model_id) const;

    //=========================================================================
    // Index Management
    //=========================================================================

    /**
     * @brief Rebuild index by scanning disk
     * @return Number of contexts indexed
     *
     * Call on startup or when index may be stale.
     */
    size_t rebuild_index();

    /**
     * @brief Register a new context in index
     * @param entry Index entry
     *
     * Called by ContextManager after successful ingestion.
     */
    void register_context(const ContextIndexEntry& entry);

    /**
     * @brief Unregister a context from index
     * @param context_id Context identifier
     *
     * Called by ContextManager after context deletion.
     */
    void unregister_context(const std::string& context_id);

    /**
     * @brief Update context access time
     * @param context_id Context identifier
     */
    void touch_context(const std::string& context_id);

    /**
     * @brief Save index to disk
     * @return true if saved
     */
    bool save_index();

    /**
     * @brief Load index from disk
     * @return true if loaded
     */
    bool load_index();

    //=========================================================================
    // Validation
    //=========================================================================

    /**
     * @brief Validate all indexed contexts still exist on disk
     * @return Number of invalid entries removed
     */
    size_t validate_index();

    /**
     * @brief Check if a specific context file is valid
     * @param context_id Context identifier
     * @return true if valid
     */
    bool is_context_valid(const std::string& context_id) const;

    //=========================================================================
    // Statistics
    //=========================================================================

    struct Stats {
        size_t total_models = 0;
        size_t total_contexts = 0;
        size_t total_storage_bytes = 0;

        size_t index_hits = 0;      ///< Lookups served from index
        size_t index_misses = 0;    ///< Required disk scan

        std::chrono::system_clock::time_point last_rebuild;
        std::chrono::system_clock::time_point last_save;
    };

    Stats get_stats() const;

    //=========================================================================
    // Callbacks
    //=========================================================================

    /**
     * @brief Callback when contexts are discovered for a model
     */
    using DiscoveryCallback = std::function<void(const ModelContextDiscovery&)>;

    /**
     * @brief Set callback for context discovery events
     */
    void set_discovery_callback(DiscoveryCallback callback);

private:
    WorkspacePaths paths_;
    ContextManager* context_manager_;

    // Index: model_id → [context entries]
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::vector<ContextIndexEntry>> model_contexts_;

    // Reverse index: context_id → model_id
    std::unordered_map<std::string, std::string> context_to_model_;

    // Statistics
    mutable Stats stats_;

    // Callbacks
    DiscoveryCallback discovery_callback_;

    // Internal helpers
    std::string get_index_path() const;
    std::vector<ContextIndexEntry> scan_disk_for_model(const std::string& model_id);
    std::vector<ContextIndexEntry> scan_all_contexts();
    DiscoveredContext entry_to_discovered(const ContextIndexEntry& entry) const;
    bool validate_entry(const ContextIndexEntry& entry) const;
};

//=============================================================================
// Integration with Model Loading
//=============================================================================

/**
 * @brief Extended model load result with auto-discovered contexts
 */
struct ModelLoadResultWithContexts {
    // Standard load result
    bool success = false;
    std::string model_id;
    std::string error_message;

    // Auto-discovered contexts
    ModelContextDiscovery contexts;

    // Convenience
    bool has_contexts() const { return !contexts.contexts.empty(); }
    size_t context_count() const { return contexts.total_contexts; }
};

} // namespace snapllm
