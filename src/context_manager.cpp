/**
 * @file context_manager.cpp
 * @brief Context Manager Implementation - vPID Level 2
 *
 * SnapLLM Context Manager - KV Cache Persistence for O(1) Query Access
 *
 * This module implements the L2 layer of vPID architecture, extending
 * the model persistence (L1) to include context-level KV cache persistence.
 *
 * Key Innovation:
 * - Pre-compute KV cache at document ingestion time (O(n²))
 * - Query uses cached KV (O(1) lookup + O(q²) for query tokens only)
 * - Same vPID philosophy: "Don't recompute what's already computed"
 */

#include "snapllm/context_manager.h"
#include "snapllm/model_manager.h"
#include "snapllm/kv_cache_extractor.h"
#include "snapllm/tiered_memory_allocator.h"
#include "llama.h"  // For llama_free
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <random>
#include <cstring>
#include <mutex>

namespace snapllm {

//=============================================================================
// Utility Functions
//=============================================================================

/**
 * @brief Generate a unique context ID
 */
std::string generate_context_id() {
    // Generate UUID-like ID: ctx_<timestamp>_<random>
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);

    std::stringstream ss;
    ss << "ctx_" << std::hex << timestamp << "_" << dis(gen);
    return ss.str();
}

/**
 * @brief Compute CRC32 checksum
 */
uint32_t compute_checksum(const void* data, size_t size) {
    // Simple CRC32 implementation
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }

    return ~crc;
}

//=============================================================================
// ContextManager Implementation
//=============================================================================

ContextManager::ContextManager(
    ModelManager* model_manager,
    const WorkspacePaths& paths
)
    : model_manager_(model_manager)
    , paths_(paths)
    , allocator_(nullptr)
{
    // Create default TieredMemoryAllocator
    TieredAllocatorConfig config;
    config.gpu_capacity_bytes = 6ULL * 1024 * 1024 * 1024;  // 6GB GPU
    config.cpu_capacity_bytes = 8ULL * 1024 * 1024 * 1024;  // 8GB CPU
    allocator_ = std::make_shared<TieredMemoryAllocator>(config);

    // Create persistent KV extractor to avoid double-free issues
    kv_extractor_ = std::make_unique<KVCacheExtractor>(model_manager_);

    std::cout << "[SnapLLM] ContextManager initialized (vPID L2)" << std::endl;
    std::cout << "[SnapLLM] Context workspace: " << paths_.contexts.string() << std::endl;
    std::cout << "[SnapLLM] Tiers: hot=" << paths_.contexts_hot.string()
              << ", warm=" << paths_.contexts_warm.string()
              << ", cold=" << paths_.contexts_cold.string() << std::endl;

    // Restore persisted contexts from disk
    restore_persisted_contexts();
}

ContextManager::ContextManager(
    ModelManager* model_manager,
    const WorkspacePaths& paths,
    std::shared_ptr<IMemoryAllocator> allocator
)
    : model_manager_(model_manager)
    , paths_(paths)
    , allocator_(allocator)
{
    // Create persistent KV extractor to avoid double-free issues
    kv_extractor_ = std::make_unique<KVCacheExtractor>(model_manager_);

    std::cout << "[SnapLLM] ContextManager initialized with custom allocator (vPID L2)" << std::endl;
    std::cout << "[SnapLLM] Context workspace: " << paths_.contexts.string() << std::endl;

    // Restore persisted contexts from disk
    restore_persisted_contexts();
}

ContextManager::~ContextManager() {
    std::cout << "[SnapLLM] ContextManager shutting down" << std::endl;

    // Save all dirty contexts to disk
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto& [id, entry] : contexts_) {
        if (entry.dirty) {
            save_to_disk(entry);
        }
    }
}

//=============================================================================
// Lifecycle Operations
//=============================================================================

ContextHandle ContextManager::generate_handle() {
    uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);

    std::stringstream ss;
    ss << "ctx_" << std::hex << std::setfill('0') << std::setw(16) << id;

    ContextHandle handle;
    handle.id = ss.str();
    handle.valid = true;
    handle.created_at = std::chrono::system_clock::now();

    return handle;
}

std::future<ContextHandle> ContextManager::ingest_async(const ContextSpec& spec) {
    return std::async(std::launch::async, [this, spec]() {
        return ingest_sync(spec);
    });
}

ContextHandle ContextManager::ingest_sync(const ContextSpec& spec) {
    std::cout << "[SnapLLM] Ingesting context for model '" << spec.model_id << "'" << std::endl;
    std::cout << "[SnapLLM] Content length: " << spec.content.size() << " chars" << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    // Generate handle
    ContextHandle handle = generate_handle();

    // Compute KV cache (the expensive O(n²) operation)
    KVCache kv_cache;
    try {
        kv_cache = compute_kv_cache(spec.content, spec.model_id, spec.config);
    } catch (const std::exception& e) {
        std::cerr << "[SnapLLM] Failed to compute KV cache: " << e.what() << std::endl;
        handle.valid = false;
        return handle;
    }

    // Set cache metadata
    kv_cache.context_id = handle.id;
    kv_cache.model_id = spec.model_id;

    // Create context entry
    ContextEntry entry;
    entry.handle = handle;
    entry.kv_cache = std::make_unique<KVCache>(std::move(kv_cache));
    entry.tier = "hot";  // Start in hot tier
    entry.dirty = true;

    // Fill metadata
    entry.metadata.id = handle.id;  // Use 'id' from ResourceMetadata base
    entry.metadata.name = spec.name.empty() ? handle.id : spec.name;
    entry.metadata.model_id = spec.model_id;
    entry.metadata.shape = entry.kv_cache->shape;
    entry.metadata.token_count = entry.kv_cache->shape.sequence_length;
    entry.metadata.content_hash = spec.content_hash;
    entry.metadata.source = spec.source;
    entry.metadata.tier = "hot";
    entry.metadata.storage_size_bytes = entry.kv_cache->memory_bytes();
    entry.metadata.ttl_seconds = spec.ttl_seconds;
    entry.metadata.priority = spec.priority;
    entry.metadata.status = ResourceStatus::Ready;
    entry.metadata.stats.created_at = std::chrono::system_clock::now();
    entry.metadata.stats.last_accessed = entry.metadata.stats.created_at;

    if (spec.ttl_seconds > 0) {
        entry.metadata.expires_at = entry.metadata.stats.created_at +
            std::chrono::seconds(spec.ttl_seconds);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "[SnapLLM] Context '" << handle.id << "' ingested in "
              << duration_ms.count() << "ms" << std::endl;
    std::cout << "[SnapLLM] KV cache size: "
              << (entry.kv_cache->memory_bytes() / (1024 * 1024)) << " MB" << std::endl;

    // Store in map
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        contexts_[handle.id] = std::move(entry);
        stats_.total_contexts++;
        stats_.hot_contexts++;
        stats_.total_memory_bytes += contexts_[handle.id].kv_cache->memory_bytes();
        stats_.hot_memory_bytes += contexts_[handle.id].kv_cache->memory_bytes();

        // MCB: Register in hash index for O(1) lookup by content
        if (!spec.content_hash.empty()) {
            std::string key = spec.model_id + ":" + spec.content_hash;
            hash_index_[key] = handle.id;
        }
    }

    // Save to disk asynchronously (background task)
    std::thread([this, id = handle.id]() {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = contexts_.find(id);
        if (it != contexts_.end()) {
            save_to_disk(it->second);
        }
    }).detach();

    return handle;
}

bool ContextManager::unload(const ContextHandle& handle) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = contexts_.find(handle.id);
    if (it == contexts_.end()) {
        return false;
    }

    // Save to disk if dirty
    if (it->second.dirty) {
        save_to_disk(it->second);
    }

    // Update stats
    size_t mem = it->second.kv_cache ? it->second.kv_cache->memory_bytes() : 0;
    stats_.total_memory_bytes -= mem;

    if (it->second.tier == "hot") {
        stats_.hot_contexts--;
        stats_.hot_memory_bytes -= mem;
    } else if (it->second.tier == "warm") {
        stats_.warm_contexts--;
        stats_.warm_memory_bytes -= mem;
    }

    // Clear KV cache from memory (keep metadata)
    it->second.kv_cache.reset();
    it->second.tier = "cold";
    it->second.metadata.status = ResourceStatus::Evicted;

    std::cout << "[SnapLLM] Context '" << handle.id << "' unloaded from memory" << std::endl;

    return true;
}

bool ContextManager::remove(const ContextHandle& handle) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = contexts_.find(handle.id);
    if (it == contexts_.end()) {
        return false;
    }

    // Update stats
    size_t mem = it->second.kv_cache ? it->second.kv_cache->memory_bytes() : 0;
    stats_.total_memory_bytes -= mem;
    stats_.total_contexts--;

    if (it->second.tier == "hot") {
        stats_.hot_contexts--;
        stats_.hot_memory_bytes -= mem;
    } else if (it->second.tier == "warm") {
        stats_.warm_contexts--;
        stats_.warm_memory_bytes -= mem;
    } else {
        stats_.cold_contexts--;
        stats_.cold_memory_bytes -= mem;
    }

    // Delete from disk
    std::error_code ec;
    for (const auto& tier : {"hot", "warm", "cold"}) {
        auto path = paths_.get_context_cache_path(handle.id, tier);
        if (fs::exists(path)) {
            fs::remove(path, ec);
        }
    }
    auto meta_path = paths_.get_context_metadata_path(handle.id);
    if (fs::exists(meta_path)) {
        fs::remove(meta_path, ec);
    }

    // Remove from map
    contexts_.erase(it);

    std::cout << "[SnapLLM] Context '" << handle.id << "' removed completely" << std::endl;

    return true;
}

bool ContextManager::is_loaded(const ContextHandle& handle) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = contexts_.find(handle.id);
    if (it == contexts_.end()) {
        return false;
    }

    return it->second.kv_cache != nullptr;
}

bool ContextManager::ensure_loaded(const ContextHandle& handle) {
    // Check if already loaded
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = contexts_.find(handle.id);
        if (it != contexts_.end() && it->second.kv_cache != nullptr) {
            return true;  // Already loaded
        }
    }

    // Need to reload from disk
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = contexts_.find(handle.id);
    if (it == contexts_.end()) {
        // Try to load from disk
        ContextEntry entry;
        if (!load_from_disk(handle.id, entry)) {
            return false;
        }
        contexts_[handle.id] = std::move(entry);
        it = contexts_.find(handle.id);
    }

    if (it->second.kv_cache != nullptr) {
        return true;  // Already loaded by another thread
    }

    // Load from disk
    if (!load_from_disk(handle.id, it->second)) {
        return false;
    }

    it->second.tier = "warm";  // Reloaded goes to warm tier
    it->second.metadata.status = ResourceStatus::Ready;

    std::cout << "[SnapLLM] Context '" << handle.id << "' reloaded from disk" << std::endl;

    return true;
}

//=============================================================================
// Query Operations
//=============================================================================

ContextQueryResult ContextManager::query(
    const ContextHandle& handle,
    const std::string& query,
    const ContextQueryConfig& config
) {
    ContextQueryResult result;
    auto start = std::chrono::high_resolution_clock::now();

    // Ensure context is loaded
    if (!ensure_loaded(handle)) {
        result.text = "Error: Context not found or could not be loaded";
        stats_.cache_misses++;
        return result;
    }

    // Get context entry
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = contexts_.find(handle.id);
    if (it == contexts_.end() || !it->second.kv_cache) {
        result.text = "Error: Context not available";
        stats_.cache_misses++;
        return result;
    }

    // Update access tracking
    update_access(handle.id);

    // Get model for inference
    const std::string& model_id = it->second.metadata.model_id;

    // Get KV cache view and raw state
    KVCacheView kv_view(it->second.kv_cache.get());

    // Get the raw KV state for injection
    // The state is stored in layers[0].keys for raw llama.cpp format
    std::vector<uint8_t> kv_state;
    if (!it->second.kv_cache->layers.empty()) {
        kv_state = it->second.kv_cache->layers[0].keys;
    }

    lock.unlock();  // Release lock during inference

    std::cout << "[SnapLLM] Query with cached context '" << handle.id << "'" << std::endl;
    std::cout << "[SnapLLM] Context tokens: " << kv_view.sequence_length()
              << ", Query: \"" << query.substr(0, 50) << (query.size() > 50 ? "..." : "") << "\"" << std::endl;

    // Inject the pre-computed KV cache using persistent extractor
    KVInjectionResult inject_result;
    if (!kv_state.empty()) {
        inject_result = kv_extractor_->inject(model_id, kv_state, 0);

        if (!inject_result.success) {
            std::cerr << "[SnapLLM] KV injection failed: " << inject_result.error_message << std::endl;
            result.text = "Error: Failed to inject KV cache - " + inject_result.error_message;
            result.cache_hit = false;
            stats_.cache_misses++;
            return result;
        }

        std::cout << "[SnapLLM] KV cache injected in " << inject_result.inject_time_ms << "ms" << std::endl;
    }

    // Generate text using the injected KV cache (vPID L2 O(1) context!)
    result.cache_hit = true;
    result.usage.context_tokens = kv_view.sequence_length();

    if (inject_result.ctx && model_manager_) {
        auto bridge = model_manager_->get_bridge();
        if (bridge) {
            // Use the new generate_with_injected_kv method
            // This skips context prefill and starts generating from after cached context
            int context_token_count = static_cast<int>(kv_view.sequence_length());

            std::cout << "[SnapLLM] vPID L2 Generation: context=" << context_token_count
                      << " tokens (skipped), query=\"" << query.substr(0, 40)
                      << (query.size() > 40 ? "..." : "") << "\"" << std::endl;

            result.text = bridge->generate_with_injected_kv(
                model_id,
                inject_result.ctx,
                query,
                context_token_count,
                config.max_tokens,
                config.temperature,
                config.top_p,
                config.top_k,
                config.repeat_penalty
            );

            // Estimate tokens generated (actual count is logged by generate_with_injected_kv)
            result.usage.generated_tokens = static_cast<uint32_t>(result.text.size() / 4);

            // NOTE: Do NOT free inject_result.ctx - owned by KVCacheExtractor
        } else {
            result.text = "[Error: VPIDBridge not available]";
        }
    } else {
        result.text = "[Error: KV injection context not available]";
    }

    result.usage.query_tokens = static_cast<uint32_t>(query.size() / 4);  // Rough estimate

    auto end = std::chrono::high_resolution_clock::now();
    result.latency_ms = std::chrono::duration<double, std::milli>(end - start).count();

    // Update stats
    stats_.queries_total++;
    stats_.cache_hits++;
    query_latency_sum_.fetch_add(static_cast<uint64_t>(result.latency_ms * 1000));
    query_count_.fetch_add(1);

    return result;
}

size_t ContextManager::query_streaming(
    const ContextHandle& handle,
    const std::string& query,
    TokenCallback callback,
    const ContextQueryConfig& config
) {
    // Ensure context is loaded
    if (!ensure_loaded(handle)) {
        callback("Error: Context not found", -1, true);
        return 0;
    }

    // Get context entry
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = contexts_.find(handle.id);
    if (it == contexts_.end() || !it->second.kv_cache) {
        callback("Error: Context not available", -1, true);
        return 0;
    }

    update_access(handle.id);

    const std::string& model_id = it->second.metadata.model_id;
    KVCacheView kv_view(it->second.kv_cache.get());

    // Get the raw KV state for injection
    std::vector<uint8_t> kv_state;
    if (!it->second.kv_cache->layers.empty()) {
        kv_state = it->second.kv_cache->layers[0].keys;
    }

    lock.unlock();

    std::cout << "[SnapLLM] Streaming query with cached context '" << handle.id << "'" << std::endl;
    std::cout << "[SnapLLM] Context tokens: " << kv_view.sequence_length() << std::endl;

    // If KV state is empty, we can't do cached streaming - signal caller to use regular path
    if (kv_state.empty()) {
        std::cout << "[SnapLLM] No KV cache data available, falling back to regular streaming" << std::endl;
        callback("", -1, true);  // Signal no cached generation available
        stats_.cache_misses++;
        return 0;
    }

    // Inject KV cache using persistent extractor (avoids double-free on function exit)
    KVInjectionResult inject_result = kv_extractor_->inject(model_id, kv_state, 0);

    if (!inject_result.success) {
        callback("Error: KV injection failed - " + inject_result.error_message, -1, true);
        stats_.cache_misses++;
        return 0;
    }

    std::cout << "[SnapLLM] KV cache injected for streaming in "
              << inject_result.inject_time_ms << "ms" << std::endl;

    // Get the bridge for streaming generation
    auto bridge = model_manager_->get_bridge();
    if (!bridge) {
        callback("Error: Could not get bridge for streaming", -1, true);
        // NOTE: Do NOT free inject_result.ctx - owned by KVCacheExtractor
        return 0;
    }

    // Perform streaming generation with the injected KV cache
    int context_token_count = static_cast<int>(kv_view.sequence_length());

    // Wrap our void callback to match VPIDBridge's bool callback signature
    auto bridge_callback = [&callback](const std::string& token, int token_id, bool is_eos) -> bool {
        callback(token, token_id, is_eos);
        return true;  // Always continue (the bridge handles EOS internally)
    };

    size_t tokens_generated = bridge->generate_streaming_with_injected_kv(
        model_id,
        inject_result.ctx,
        query,
        context_token_count,
        bridge_callback,
        static_cast<int>(config.max_tokens),
        config.temperature,
        config.top_p,
        config.top_k,
        config.repeat_penalty
    );

    // NOTE: Do NOT free inject_result.ctx here!
    // The context is owned and cached by KVCacheExtractor for reuse.
    // KVCacheExtractor::~KVCacheExtractor() handles cleanup.

    stats_.queries_total++;
    stats_.cache_hits++;

    return tokens_generated;
}

ContextQueryResult ContextManager::query_multi(
    const std::vector<ContextHandle>& handles,
    const std::string& query,
    const ContextQueryConfig& config
) {
    ContextQueryResult result;

    // Ensure all contexts are loaded
    for (const auto& handle : handles) {
        if (!ensure_loaded(handle)) {
            result.text = "Error: One or more contexts could not be loaded";
            return result;
        }
    }

    // TODO: Implement multi-context RAG with merged KV caches
    // This would concatenate or interleave KV caches from multiple contexts

    std::cout << "[SnapLLM] Multi-context query with " << handles.size() << " contexts" << std::endl;

    result.text = "[Placeholder: Multi-context RAG will be implemented in Phase 1.3]";
    result.cache_hit = true;

    stats_.queries_total++;

    return result;
}

//=============================================================================
// Access Operations
//=============================================================================

KVCacheView ContextManager::get_kv_cache(const ContextHandle& handle) {
    if (!ensure_loaded(handle)) {
        return KVCacheView();
    }

    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = contexts_.find(handle.id);
    if (it == contexts_.end() || !it->second.kv_cache) {
        return KVCacheView();
    }

    update_access(handle.id);
    return KVCacheView(it->second.kv_cache.get());
}

std::optional<ContextMetadata> ContextManager::get_metadata(const ContextHandle& handle) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = contexts_.find(handle.id);
    if (it == contexts_.end()) {
        return std::nullopt;
    }

    return it->second.metadata;
}

ContextStatus ContextManager::get_status(const ContextHandle& handle) const {
    ContextStatus status;
    status.context_id = handle.id;

    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = contexts_.find(handle.id);
    if (it == contexts_.end()) {
        status.state = ResourceStatus::Unknown;
        return status;
    }

    const auto& entry = it->second;
    status.state = entry.metadata.status;
    status.token_count = entry.metadata.token_count;
    status.num_layers = entry.metadata.shape.num_layers;
    status.memory_bytes = entry.kv_cache ? entry.kv_cache->memory_bytes() : 0;
    status.tier = entry.tier;
    status.access_count = entry.kv_cache ? entry.kv_cache->access_count : 0;
    status.created_at = entry.metadata.stats.created_at;
    status.last_accessed = entry.metadata.stats.last_accessed;
    status.progress = entry.metadata.status == ResourceStatus::Ready ? 1.0f : 0.0f;

    return status;
}

std::vector<ContextHandle> ContextManager::list() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<ContextHandle> handles;
    handles.reserve(contexts_.size());

    for (const auto& [id, entry] : contexts_) {
        handles.push_back(entry.handle);
    }

    return handles;
}

std::vector<ContextHandle> ContextManager::list_by_tier(const std::string& tier) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<ContextHandle> handles;

    for (const auto& [id, entry] : contexts_) {
        if (tier == "all" || entry.tier == tier) {
            handles.push_back(entry.handle);
        }
    }

    return handles;
}

std::vector<ContextHandle> ContextManager::list_by_model(const std::string& model_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<ContextHandle> handles;

    for (const auto& [id, entry] : contexts_) {
        if (entry.metadata.model_id == model_id) {
            handles.push_back(entry.handle);
        }
    }

    return handles;
}

//=============================================================================
// Tiering Operations
//=============================================================================

bool ContextManager::promote(const ContextHandle& handle, const std::string& target_tier) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = contexts_.find(handle.id);
    if (it == contexts_.end()) {
        return false;
    }

    const std::string& current_tier = it->second.tier;

    // Check valid promotion
    if (target_tier == "hot" && (current_tier == "warm" || current_tier == "cold")) {
        // Ensure loaded into memory
        if (!it->second.kv_cache) {
            lock.unlock();
            if (!ensure_loaded(handle)) {
                return false;
            }
            lock.lock();
            it = contexts_.find(handle.id);
        }

        // Use allocator for tier promotion if available
        if (allocator_) {
            MemoryTier mem_target = MemoryTier::GPU_HBM;
            if (!allocator_->promote(handle.id, mem_target)) {
                std::cout << "[SnapLLM] Allocator promotion to GPU failed, using CPU" << std::endl;
            }
        }

        // Update tier stats
        size_t mem = it->second.kv_cache->memory_bytes();
        if (current_tier == "warm") {
            stats_.warm_contexts--;
            stats_.warm_memory_bytes -= mem;
        } else {
            stats_.cold_contexts--;
            stats_.cold_memory_bytes -= mem;
        }
        stats_.hot_contexts++;
        stats_.hot_memory_bytes += mem;

        it->second.tier = "hot";
        it->second.metadata.tier = "hot";
        it->second.dirty = true;

        std::cout << "[SnapLLM] Context '" << handle.id << "' promoted to hot tier" << std::endl;
        return true;
    }

    if (target_tier == "warm" && current_tier == "cold") {
        // Load into CPU memory
        if (!it->second.kv_cache) {
            lock.unlock();
            if (!ensure_loaded(handle)) {
                return false;
            }
            lock.lock();
            it = contexts_.find(handle.id);
        }

        // Use allocator for tier promotion if available
        if (allocator_) {
            allocator_->promote(handle.id, MemoryTier::CPU_RAM);
        }

        size_t mem = it->second.kv_cache->memory_bytes();
        stats_.cold_contexts--;
        stats_.cold_memory_bytes -= mem;
        stats_.warm_contexts++;
        stats_.warm_memory_bytes += mem;

        it->second.tier = "warm";
        it->second.metadata.tier = "warm";

        std::cout << "[SnapLLM] Context '" << handle.id << "' promoted to warm tier" << std::endl;
        return true;
    }

    return false;  // Invalid promotion
}

bool ContextManager::demote(const ContextHandle& handle, const std::string& target_tier) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = contexts_.find(handle.id);
    if (it == contexts_.end()) {
        return false;
    }

    const std::string& current_tier = it->second.tier;
    size_t mem = it->second.kv_cache ? it->second.kv_cache->memory_bytes() : 0;

    if (target_tier == "warm" && current_tier == "hot") {
        // Use allocator for tier demotion if available
        if (allocator_) {
            allocator_->demote(handle.id, MemoryTier::CPU_RAM);
        }

        stats_.hot_contexts--;
        stats_.hot_memory_bytes -= mem;
        stats_.warm_contexts++;
        stats_.warm_memory_bytes += mem;

        it->second.tier = "warm";
        it->second.metadata.tier = "warm";

        std::cout << "[SnapLLM] Context '" << handle.id << "' demoted to warm tier" << std::endl;
        return true;
    }

    if (target_tier == "cold" && (current_tier == "hot" || current_tier == "warm")) {
        // Save to disk and free memory
        if (it->second.dirty) {
            save_to_disk(it->second);
        }

        // Use allocator for tier demotion (to SSD = memory free)
        if (allocator_) {
            allocator_->demote(handle.id, MemoryTier::SSD_NVME);
        }

        if (current_tier == "hot") {
            stats_.hot_contexts--;
            stats_.hot_memory_bytes -= mem;
        } else {
            stats_.warm_contexts--;
            stats_.warm_memory_bytes -= mem;
        }
        stats_.cold_contexts++;
        stats_.cold_memory_bytes += mem;

        stats_.total_memory_bytes -= mem;

        it->second.kv_cache.reset();
        it->second.tier = "cold";
        it->second.metadata.tier = "cold";
        it->second.metadata.status = ResourceStatus::Evicted;

        std::cout << "[SnapLLM] Context '" << handle.id << "' demoted to cold tier (memory freed)" << std::endl;
        return true;
    }

    return false;
}

bool ContextManager::warm(const ContextHandle& handle) {
    return promote(handle, "hot");
}

//=============================================================================
// Statistics
//=============================================================================

size_t ContextManager::memory_usage() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return stats_.total_memory_bytes;
}

size_t ContextManager::count() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return contexts_.size();
}

ContextManager::Stats ContextManager::get_stats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    Stats stats = stats_;

    // Calculate average latency
    uint64_t count = query_count_.load();
    if (count > 0) {
        stats.avg_query_latency_ms = static_cast<double>(query_latency_sum_.load()) / (count * 1000.0);
    }

    return stats;
}

//=============================================================================
// Configuration
//=============================================================================

void ContextManager::set_tier_capacity(const std::string& tier, size_t bytes) {
    // TODO: Store and enforce tier capacity limits
    std::cout << "[SnapLLM] Set " << tier << " tier capacity to "
              << (bytes / (1024 * 1024 * 1024)) << " GB" << std::endl;
}

void ContextManager::set_default_ttl(uint32_t seconds) {
    default_ttl_seconds_ = seconds;
    std::cout << "[SnapLLM] Default TTL set to " << seconds << " seconds" << std::endl;
}

void ContextManager::set_auto_tiering(bool enabled) {
    auto_tiering_enabled_ = enabled;
    std::cout << "[SnapLLM] Auto-tiering " << (enabled ? "enabled" : "disabled") << std::endl;
}

//=============================================================================
// Private Helpers
//=============================================================================

KVCache ContextManager::compute_kv_cache(
    const std::string& content,
    const std::string& model_id,
    const KVCacheConfig& config
) {
    std::cout << "[SnapLLM] Computing KV cache (O(n²) operation)..." << std::endl;

    // Create KV cache extractor using ModelManager
    KVCacheExtractor extractor(model_manager_);

    // Check if model supports extraction
    if (!extractor.supports_extraction(model_id)) {
        std::cerr << "[SnapLLM] Model '" << model_id << "' does not support KV extraction" << std::endl;
        std::cerr << "[SnapLLM] Ensure the model is loaded in ModelManager" << std::endl;

        // Return placeholder for testing without loaded model
        KVCache cache;
        cache.context_id = "";
        cache.model_id = model_id;
        cache.shape.num_layers = 32;
        cache.shape.num_heads = 32;
        cache.shape.head_dim = 128;
        cache.shape.sequence_length = static_cast<uint32_t>(content.size() / 4);
        cache.shape.dtype = config.dtype;
        cache.allocate();
        return cache;
    }

    // Configure extraction
    KVExtractionConfig extract_config;
    extract_config.sequence_id = 0;
    extract_config.batch_size = 512;
    extract_config.verbose = true;

    // Extract KV cache using llama.cpp integration
    KVExtractionResult result = extractor.extract(model_id, content, extract_config);

    if (!result.success) {
        std::cerr << "[SnapLLM] KV extraction failed: " << result.error_message << std::endl;
        throw std::runtime_error("KV cache extraction failed: " + result.error_message);
    }

    // Convert llama.cpp state to our KVCache format
    KVCache cache = extractor.convert_to_kv_cache(model_id, result.kv_state, result.token_count);
    cache.model_id = model_id;

    std::cout << "[SnapLLM] KV cache computed: "
              << cache.shape.num_layers << " layers, "
              << result.token_count << " tokens" << std::endl;
    std::cout << "[SnapLLM] Timing - Tokenize: " << result.tokenize_time_ms << "ms, "
              << "Prefill: " << result.prefill_time_ms << "ms, "
              << "Extract: " << result.extract_time_ms << "ms, "
              << "Total: " << result.total_time_ms << "ms" << std::endl;

    return cache;
}

bool ContextManager::save_to_disk(const ContextEntry& entry) {
    if (!entry.kv_cache) {
        return false;
    }

    // Determine path based on tier
    auto path = paths_.get_context_cache_path(entry.handle.id, entry.tier);

    std::cout << "[SnapLLM] Saving context '" << entry.handle.id << "' to " << path.string() << std::endl;

    // Create directories if needed
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    // Write file
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "[SnapLLM] Failed to create file: " << path.string() << std::endl;
        return false;
    }

    // Write header
    KVCacheFileHeader header;
    header.version = 1;
    header.set_context_id(entry.handle.id);
    header.set_model_id(entry.metadata.model_id);
    header.set_shape(entry.kv_cache->shape);
    header.data_size = entry.kv_cache->memory_bytes();
    header.created_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        entry.metadata.stats.created_at.time_since_epoch()
    ).count();

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Write layer data
    for (const auto& layer : entry.kv_cache->layers) {
        file.write(reinterpret_cast<const char*>(layer.keys.data()), layer.keys.size());
        file.write(reinterpret_cast<const char*>(layer.values.data()), layer.values.size());
    }

    // Compute and update checksum
    // TODO: Implement proper checksum

    file.close();

    // Also save metadata JSON
    auto meta_path = paths_.get_context_metadata_path(entry.handle.id);
    fs::create_directories(meta_path.parent_path(), ec);

    std::ofstream meta_file(meta_path);
    if (meta_file) {
        meta_file << "{\n";
        meta_file << "  \"context_id\": \"" << entry.handle.id << "\",\n";
        meta_file << "  \"model_id\": \"" << entry.metadata.model_id << "\",\n";
        meta_file << "  \"token_count\": " << entry.metadata.token_count << ",\n";
        meta_file << "  \"tier\": \"" << entry.tier << "\",\n";
        meta_file << "  \"storage_size_bytes\": " << entry.metadata.storage_size_bytes << ",\n";
        meta_file << "  \"num_layers\": " << entry.metadata.shape.num_layers << ",\n";
        meta_file << "  \"num_heads\": " << entry.metadata.shape.num_heads << ",\n";
        meta_file << "  \"head_dim\": " << entry.metadata.shape.head_dim << ",\n";
        meta_file << "  \"sequence_length\": " << entry.metadata.shape.sequence_length << "\n";
        meta_file << "}\n";
        meta_file.close();
    }

    std::cout << "[SnapLLM] Context saved successfully" << std::endl;

    return true;
}

bool ContextManager::load_from_disk(const std::string& context_id, ContextEntry& entry) {
    // Try each tier
    for (const auto& tier : {"hot", "warm", "cold"}) {
        auto path = paths_.get_context_cache_path(context_id, tier);
        if (!fs::exists(path)) {
            continue;
        }

        std::cout << "[SnapLLM] Loading context '" << context_id << "' from " << path.string() << std::endl;

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            continue;
        }

        // Read header
        KVCacheFileHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(header));

        if (!header.is_valid()) {
            std::cerr << "[SnapLLM] Invalid KV cache file header" << std::endl;
            continue;
        }

        // Create KV cache from header
        auto kv_cache = std::make_unique<KVCache>();
        kv_cache->context_id = header.get_context_id();
        kv_cache->model_id = header.get_model_id();
        kv_cache->shape = header.get_shape();
        kv_cache->allocate();

        // Read layer data
        for (auto& layer : kv_cache->layers) {
            file.read(reinterpret_cast<char*>(layer.keys.data()), layer.keys.size());
            file.read(reinterpret_cast<char*>(layer.values.data()), layer.values.size());
        }

        // Fill entry
        entry.handle.id = context_id;
        entry.handle.valid = true;
        entry.kv_cache = std::move(kv_cache);
        entry.tier = "warm";  // Loaded from disk goes to warm
        entry.dirty = false;

        entry.metadata.id = context_id;
        entry.metadata.model_id = header.get_model_id();
        entry.metadata.shape = header.get_shape();
        entry.metadata.token_count = header.sequence_length;
        entry.metadata.tier = "warm";
        entry.metadata.storage_size_bytes = header.data_size;
        entry.metadata.status = ResourceStatus::Ready;

        std::cout << "[SnapLLM] Context loaded: " << entry.kv_cache->shape.sequence_length
                  << " tokens, " << (entry.kv_cache->memory_bytes() / (1024 * 1024)) << " MB" << std::endl;

        return true;
    }

    return false;
}

void ContextManager::restore_persisted_contexts() {
    // Scan metadata folder for persisted contexts
    auto metadata_path = paths_.contexts / "metadata";
    if (!fs::exists(metadata_path)) {
        std::cout << "[SnapLLM] No persisted contexts found" << std::endl;
        return;
    }

    int restored_count = 0;
    size_t total_size = 0;

    for (const auto& entry : fs::directory_iterator(metadata_path)) {
        if (entry.path().extension() != ".json") {
            continue;
        }

        std::string context_id = entry.path().stem().string();

        // Read metadata JSON
        std::ifstream meta_file(entry.path());
        if (!meta_file) {
            continue;
        }

        std::string json_content((std::istreambuf_iterator<char>(meta_file)),
                                  std::istreambuf_iterator<char>());
        meta_file.close();

        // Parse basic fields (simple parsing without full JSON library)
        auto extract_field = [&](const std::string& field) -> std::string {
            size_t pos = json_content.find("\"" + field + "\"");
            if (pos == std::string::npos) return "";
            pos = json_content.find(":", pos);
            if (pos == std::string::npos) return "";
            pos++;
            while (pos < json_content.size() && (json_content[pos] == ' ' || json_content[pos] == '"')) pos++;
            size_t end = pos;
            while (end < json_content.size() && json_content[end] != '"' && json_content[end] != ',' && json_content[end] != '\n') end++;
            return json_content.substr(pos, end - pos);
        };

        std::string model_id = extract_field("model_id");
        std::string tier = extract_field("tier");
        int token_count = 0;
        size_t storage_size = 0;

        try {
            token_count = std::stoi(extract_field("token_count"));
            storage_size = std::stoull(extract_field("storage_size_bytes"));
        } catch (...) {}

        // Check if KV cache file exists
        bool has_kv_file = false;
        for (const auto& t : {"hot", "warm", "cold"}) {
            auto kv_path = paths_.get_context_cache_path(context_id, t);
            if (fs::exists(kv_path)) {
                has_kv_file = true;
                tier = t;
                break;
            }
        }

        if (!has_kv_file) {
            continue;
        }

        // Register in context index (don't load KV data yet - lazy loading)
        ContextEntry ctx_entry;
        ctx_entry.handle.id = context_id;
        ctx_entry.handle.valid = true;
        ctx_entry.tier = "cold";  // Start as cold (on disk)
        ctx_entry.dirty = false;
        ctx_entry.kv_cache = nullptr;  // Not loaded yet

        ctx_entry.metadata.id = context_id;
        ctx_entry.metadata.model_id = model_id;
        ctx_entry.metadata.token_count = token_count;
        ctx_entry.metadata.tier = "cold";
        ctx_entry.metadata.storage_size_bytes = storage_size;
        ctx_entry.metadata.status = ResourceStatus::Evicted;

        contexts_[context_id] = std::move(ctx_entry);
        stats_.total_contexts++;
        stats_.cold_contexts++;
        total_size += storage_size;
        restored_count++;

        // Add to hash index for model lookups
        std::string key = model_id + ":" + context_id;
        hash_index_[key] = context_id;
    }

    if (restored_count > 0) {
        std::cout << "[SnapLLM] Restored " << restored_count << " persisted contexts ("
                  << (total_size / (1024 * 1024)) << " MB on disk)" << std::endl;
    } else {
        std::cout << "[SnapLLM] No persisted contexts to restore" << std::endl;
    }
}

void ContextManager::update_access(const std::string& context_id) {
    // Note: Caller should hold at least shared lock
    auto it = contexts_.find(context_id);
    if (it != contexts_.end()) {
        it->second.metadata.stats.last_accessed = std::chrono::system_clock::now();
        if (it->second.kv_cache) {
            it->second.kv_cache->touch();
        }
    }
}

void ContextManager::check_tiering() {
    if (!auto_tiering_enabled_) {
        return;
    }

    // TODO: Implement auto-tiering based on:
    // - Access frequency
    // - Tier capacity limits
    // - TTL expiration
    // - Memory pressure

    // This would run periodically or on memory pressure events
}

//=============================================================================
// MCB Auto-Context Operations (Model Context Bucket)
//=============================================================================

std::string ContextManager::compute_content_hash(const std::string& content) {
    // Simple hash using CRC32 + size for fast lookup
    // For production, consider SHA256 for collision resistance
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(content.data());
    size_t size = content.size();

    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    crc = ~crc;

    // Combine CRC32 with content length for uniqueness
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(8) << crc
       << "_" << std::setw(8) << size;
    return ss.str();
}

std::optional<ContextHandle> ContextManager::find_by_hash(
    const std::string& model_id,
    const std::string& content_hash
) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    // Build lookup key: model_id:content_hash
    std::string key = model_id + ":" + content_hash;

    auto it = hash_index_.find(key);
    if (it == hash_index_.end()) {
        return std::nullopt;
    }

    // Get the context handle
    auto ctx_it = contexts_.find(it->second);
    if (ctx_it == contexts_.end()) {
        return std::nullopt;
    }

    return ctx_it->second.handle;
}

ContextHandle ContextManager::find_or_create(
    const std::string& model_id,
    const std::string& content,
    const std::string& name
) {
    // Compute content hash
    std::string content_hash = compute_content_hash(content);
    std::string key = model_id + ":" + content_hash;

    std::cout << "[SnapLLM MCB] find_or_create for model '" << model_id
              << "', content hash: " << content_hash << std::endl;

    // First, try to find existing context (read lock)
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = hash_index_.find(key);
        if (it != hash_index_.end()) {
            auto ctx_it = contexts_.find(it->second);
            if (ctx_it != contexts_.end()) {
                std::cout << "[SnapLLM MCB] Cache HIT! Reusing context '"
                          << ctx_it->second.handle.id << "'" << std::endl;
                stats_.cache_hits++;

                // Update access time (need upgrade to write lock conceptually,
                // but we'll do it separately to avoid deadlock)
                const_cast<ContextEntry&>(ctx_it->second).metadata.stats.last_accessed =
                    std::chrono::system_clock::now();

                return ctx_it->second.handle;
            }
        }
    }

    // Not found - need to ingest (this is the expensive O(n²) operation)
    std::cout << "[SnapLLM MCB] Cache MISS. Ingesting new context..." << std::endl;
    stats_.cache_misses++;

    // Create spec for ingestion
    ContextSpec spec;
    spec.content = content;
    spec.model_id = model_id;
    spec.content_hash = content_hash;
    spec.name = name.empty() ? ("auto_" + content_hash.substr(0, 8)) : name;
    spec.priority = "high";  // Auto-created contexts are high priority

    // Ingest synchronously (computes KV cache)
    ContextHandle handle = ingest_sync(spec);

    if (handle.valid) {
        // Add to hash index for future lookups
        std::unique_lock<std::shared_mutex> lock(mutex_);
        hash_index_[key] = handle.id;

        std::cout << "[SnapLLM MCB] Created new context '" << handle.id
                  << "', indexed for future reuse" << std::endl;
    }

    return handle;
}

} // namespace snapllm
