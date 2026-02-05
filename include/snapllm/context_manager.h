/**
 * @file context_manager.h
 * @brief Context Manager - vPID Level 2 Implementation
 *
 * SnapLLM Context Manager provides:
 * - KV cache persistence for O(1) query access
 * - Multi-tier storage (GPU → CPU → SSD)
 * - Automatic tiering based on access patterns
 * - Parallel to ModelManager (L1), extends vPID architecture
 *
 * Key Innovation:
 * - Pre-compute KV cache at ingestion time (O(n²))
 * - Query uses cached KV (O(1) lookup + O(q²) for query)
 * - Same vPID philosophy: "Don't recompute what's already computed"
 */

#pragma once

#include "interfaces/i_resource_manager.h"
#include "interfaces/i_memory_allocator.h"
#include "kv_cache.h"
#include "kv_cache_extractor.h"
#include "workspace_paths.h"

#include <string>
#include <memory>
#include <vector>
#include <optional>
#include <future>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <functional>

namespace snapllm {

// Forward declarations
class VPIDBridge;
class ModelManager;

//=============================================================================
// Context Specification
//=============================================================================

/**
 * @brief Specification for ingesting a context
 */
struct ContextSpec {
    std::string content;          ///< Text content to ingest
    std::string model_id;         ///< Model to use for KV computation
    KVCacheConfig config;         ///< KV cache configuration

    // Optional metadata
    std::string name;             ///< Human-readable name
    std::string source;           ///< Source identifier (file path, URL, etc.)
    std::string content_hash;     ///< Hash of content for deduplication

    // Lifecycle options
    uint32_t ttl_seconds = 86400; ///< Time-to-live (0 = infinite)
    std::string priority = "normal"; ///< "low", "normal", "high"

    ContextSpec() = default;

    ContextSpec(const std::string& text, const std::string& model)
        : content(text), model_id(model) {}
};

//=============================================================================
// Context Metadata
//=============================================================================

/**
 * @brief Extended metadata for contexts
 */
struct ContextMetadata : public ResourceMetadata {
    std::string model_id;
    KVCacheShape shape;

    // Content info
    uint32_t token_count = 0;
    std::string content_hash;
    std::string source;

    // Storage info
    std::string tier;             ///< "hot", "warm", "cold"
    size_t storage_size_bytes = 0;
    bool is_compressed = false;

    // Lifecycle
    uint32_t ttl_seconds = 0;
    std::chrono::system_clock::time_point expires_at;

    // Priority
    std::string priority = "normal";

    ContextMetadata() = default;
};

//=============================================================================
// Context Status
//=============================================================================

/**
 * @brief Detailed context status information
 */
struct ContextStatus {
    std::string context_id;
    ResourceStatus state = ResourceStatus::Unknown;

    // Shape info
    uint32_t token_count = 0;
    uint32_t num_layers = 0;

    // Memory info
    size_t memory_bytes = 0;
    std::string tier;

    // Access info
    uint64_t access_count = 0;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_accessed;

    // Progress (for loading state)
    float progress = 0.0f;
    std::string progress_message;
};

//=============================================================================
// Query Configuration
//=============================================================================

/**
 * @brief Configuration for queries with cached context
 */
struct ContextQueryConfig {
    uint32_t max_tokens = 1024;
    float temperature = 0.7f;
    float top_p = 0.95f;
    int top_k = 40;
    float repeat_penalty = 1.1f;
    bool stream = false;
};

/**
 * @brief Result from a context query
 */
struct ContextQueryResult {
    std::string text;
    std::vector<int> tokens;

    struct Usage {
        uint32_t context_tokens = 0;    ///< Tokens from cached context
        uint32_t query_tokens = 0;      ///< Tokens in query
        uint32_t generated_tokens = 0;  ///< Tokens generated
    } usage;

    double latency_ms = 0.0;
    bool cache_hit = false;
};

//=============================================================================
// Context Manager
//=============================================================================

/**
 * @brief Context Manager - vPID Level 2
 *
 * Manages the lifecycle of pre-computed KV caches for contexts.
 * Parallel to ModelManager (L1), provides O(1) context access.
 *
 * Usage:
 * @code
 * ContextManager ctx_mgr(&model_manager, workspace_paths);
 *
 * // Ingest a document (expensive, O(n²))
 * ContextSpec spec{"The quick brown fox...", "medicine"};
 * auto ctx_id = ctx_mgr.ingest_sync(spec);
 *
 * // Query using cached KV (fast, O(1) + O(q²))
 * auto result = ctx_mgr.query(ctx_id, "What color is the fox?", config);
 * @endcode
 */
class ContextManager {
public:
    /**
     * @brief Construct context manager
     * @param model_manager Pointer to model manager (for inference)
     * @param paths Workspace paths
     */
    ContextManager(
        ModelManager* model_manager,
        const WorkspacePaths& paths
    );

    /**
     * @brief Construct with custom memory allocator
     */
    ContextManager(
        ModelManager* model_manager,
        const WorkspacePaths& paths,
        std::shared_ptr<IMemoryAllocator> allocator
    );

    ~ContextManager();

    // Prevent copying
    ContextManager(const ContextManager&) = delete;
    ContextManager& operator=(const ContextManager&) = delete;

    //=========================================================================
    // Lifecycle Operations
    //=========================================================================

    /**
     * @brief Ingest context asynchronously
     * @param spec Context specification
     * @return Future resolving to context handle
     *
     * This is the expensive O(n²) operation that computes the KV cache.
     * Run in background for large contexts.
     */
    std::future<ContextHandle> ingest_async(const ContextSpec& spec);

    /**
     * @brief Ingest context synchronously (blocking)
     * @param spec Context specification
     * @return Context handle, or invalid handle on failure
     */
    ContextHandle ingest_sync(const ContextSpec& spec);

    /**
     * @brief Unload context from memory (keeps on disk)
     * @param handle Context handle
     * @return true if unloaded
     */
    bool unload(const ContextHandle& handle);

    /**
     * @brief Delete context completely (memory and disk)
     * @param handle Context handle
     * @return true if deleted
     */
    bool remove(const ContextHandle& handle);

    /**
     * @brief Check if context is loaded
     * @param handle Context handle
     * @return true if loaded and ready
     */
    bool is_loaded(const ContextHandle& handle) const;

    /**
     * @brief Ensure context is loaded (reload from disk if needed)
     * @param handle Context handle
     * @return true if context is ready
     */
    bool ensure_loaded(const ContextHandle& handle);

    //=========================================================================
    // Query Operations
    //=========================================================================

    /**
     * @brief Query using cached context (O(1) context lookup)
     * @param handle Context handle
     * @param query Query text
     * @param config Query configuration
     * @return Query result
     */
    ContextQueryResult query(
        const ContextHandle& handle,
        const std::string& query,
        const ContextQueryConfig& config = ContextQueryConfig{}
    );

    /**
     * @brief Query with streaming output
     * @param handle Context handle
     * @param query Query text
     * @param callback Token callback
     * @param config Query configuration
     * @return Number of tokens generated
     */
    using TokenCallback = std::function<void(const std::string& token, int token_id, bool is_done)>;
    size_t query_streaming(
        const ContextHandle& handle,
        const std::string& query,
        TokenCallback callback,
        const ContextQueryConfig& config = ContextQueryConfig{}
    );

    /**
     * @brief Query with multiple contexts (RAG)
     * @param handles Vector of context handles
     * @param query Query text
     * @param config Query configuration
     * @return Query result
     */
    ContextQueryResult query_multi(
        const std::vector<ContextHandle>& handles,
        const std::string& query,
        const ContextQueryConfig& config = ContextQueryConfig{}
    );

    //=========================================================================
    // Access Operations
    //=========================================================================

    /**
     * @brief Get KV cache view for a context
     * @param handle Context handle
     * @return KV cache view, or empty view if not found
     */
    KVCacheView get_kv_cache(const ContextHandle& handle);

    /**
     * @brief Get context metadata
     * @param handle Context handle
     * @return Metadata if exists
     */
    std::optional<ContextMetadata> get_metadata(const ContextHandle& handle) const;

    /**
     * @brief Get context status
     * @param handle Context handle
     * @return Status information
     */
    ContextStatus get_status(const ContextHandle& handle) const;

    /**
     * @brief List all context handles
     * @return Vector of handles
     */
    std::vector<ContextHandle> list() const;

    /**
     * @brief List contexts by tier
     * @param tier "hot", "warm", "cold", or "all"
     * @return Vector of handles
     */
    std::vector<ContextHandle> list_by_tier(const std::string& tier) const;

    /**
     * @brief List contexts for a model
     * @param model_id Model identifier
     * @return Vector of handles
     */
    std::vector<ContextHandle> list_by_model(const std::string& model_id) const;

    //=========================================================================
    // MCB Auto-Context Operations (Model Context Bucket)
    //=========================================================================

    /**
     * @brief Find existing context by content hash, or create new one
     * @param model_id Model identifier
     * @param content Content to hash and lookup/ingest
     * @param name Optional name for new context
     * @return Context handle (existing or newly created)
     *
     * This is the key MCB integration method:
     * - Computes SHA256 hash of content
     * - Looks up existing context with matching hash + model
     * - If found: returns existing handle (O(1) cache hit)
     * - If not found: ingests content and returns new handle
     *
     * Use for automatic context caching in chat:
     * @code
     * // Build context from conversation history
     * std::string history = build_history(messages, exclude_last=true);
     * auto ctx = context_manager.find_or_create(model_id, history, "chat_session");
     *
     * // Query with just the new user message
     * auto result = context_manager.query(ctx, last_user_message);
     * @endcode
     */
    ContextHandle find_or_create(
        const std::string& model_id,
        const std::string& content,
        const std::string& name = ""
    );

    /**
     * @brief Find context by content hash only (no creation)
     * @param model_id Model identifier
     * @param content_hash Pre-computed content hash
     * @return Context handle if found, invalid handle if not
     */
    std::optional<ContextHandle> find_by_hash(
        const std::string& model_id,
        const std::string& content_hash
    ) const;

    /**
     * @brief Compute content hash (SHA256)
     * @param content Content to hash
     * @return Hex-encoded hash string
     */
    static std::string compute_content_hash(const std::string& content);

    //=========================================================================
    // Tiering Operations
    //=========================================================================

    /**
     * @brief Promote context to faster tier
     * @param handle Context handle
     * @param target_tier "hot" or "warm"
     * @return true if promoted
     */
    bool promote(const ContextHandle& handle, const std::string& target_tier);

    /**
     * @brief Demote context to slower tier
     * @param handle Context handle
     * @param target_tier "warm" or "cold"
     * @return true if demoted
     */
    bool demote(const ContextHandle& handle, const std::string& target_tier);

    /**
     * @brief Warm up context (pre-load to hot tier)
     * @param handle Context handle
     * @return true if warmed
     */
    bool warm(const ContextHandle& handle);

    //=========================================================================
    // Statistics
    //=========================================================================

    /**
     * @brief Get total memory usage
     * @return Memory in bytes
     */
    size_t memory_usage() const;

    /**
     * @brief Get context count
     * @return Number of contexts
     */
    size_t count() const;

    /**
     * @brief Get statistics
     */
    struct Stats {
        size_t total_contexts = 0;
        size_t hot_contexts = 0;
        size_t warm_contexts = 0;
        size_t cold_contexts = 0;

        size_t total_memory_bytes = 0;
        size_t hot_memory_bytes = 0;
        size_t warm_memory_bytes = 0;
        size_t cold_memory_bytes = 0;

        uint64_t queries_total = 0;
        uint64_t cache_hits = 0;
        uint64_t cache_misses = 0;

        double avg_query_latency_ms = 0.0;
        double hit_rate() const {
            uint64_t total = cache_hits + cache_misses;
            return total > 0 ? (double)cache_hits / total : 0.0;
        }
    };

    Stats get_stats() const;

    //=========================================================================
    // Configuration
    //=========================================================================

    /**
     * @brief Set tier capacity limits
     */
    void set_tier_capacity(const std::string& tier, size_t bytes);

    /**
     * @brief Set default TTL for new contexts
     */
    void set_default_ttl(uint32_t seconds);

    /**
     * @brief Enable/disable automatic tiering
     */
    void set_auto_tiering(bool enabled);

private:
    // Internal context entry
    struct ContextEntry {
        ContextHandle handle;
        std::unique_ptr<KVCache> kv_cache;
        ContextMetadata metadata;
        std::string tier;
        bool dirty = false;  // Needs sync to disk

        ContextEntry() = default;
        ContextEntry(ContextEntry&&) = default;
        ContextEntry& operator=(ContextEntry&&) = default;
    };

    // Core components
    ModelManager* model_manager_;
    WorkspacePaths paths_;
    std::shared_ptr<IMemoryAllocator> allocator_;

    // Persistent KV cache extractor for injection (avoids double-free issues)
    mutable std::unique_ptr<KVCacheExtractor> kv_extractor_;

    // Context storage
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ContextEntry> contexts_;

    // MCB: Hash index for O(1) content lookup
    // Key: "model_id:content_hash" → context_id
    std::unordered_map<std::string, std::string> hash_index_;

    // ID generation
    std::atomic<uint64_t> next_id_{1};

    // Statistics
    mutable Stats stats_;
    std::atomic<uint64_t> query_latency_sum_{0};
    std::atomic<uint64_t> query_count_{0};

    // Configuration
    uint32_t default_ttl_seconds_ = 86400;
    bool auto_tiering_enabled_ = true;

    // Internal helpers
    ContextHandle generate_handle();
    KVCache compute_kv_cache(const std::string& content, const std::string& model_id, const KVCacheConfig& config);
    bool save_to_disk(const ContextEntry& entry);
    bool load_from_disk(const std::string& context_id, ContextEntry& entry);
    void restore_persisted_contexts();
    void update_access(const std::string& context_id);
    void check_tiering();
};

} // namespace snapllm
