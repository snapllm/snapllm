/**
 * @file kv_cache_extractor.h
 * @brief KV Cache Extraction from llama.cpp
 *
 * Provides integration with llama.cpp's state serialization API to
 * extract and restore KV cache tensors for vPID L2 context persistence.
 *
 * Key APIs Used:
 * - llama_state_seq_get_data() - Extract per-sequence KV cache
 * - llama_state_seq_set_data() - Restore per-sequence KV cache
 * - llama_decode() - Run prefill to generate KV cache
 *
 * The extractor handles:
 * - Tokenization of input text
 * - Running prefill forward pass
 * - Extracting KV state in llama.cpp's internal format
 * - Converting to our KVCache format for persistent storage
 */

#pragma once

#include "kv_cache.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <mutex>

// Forward declarations for llama.cpp types
struct llama_model;
struct llama_context;

namespace snapllm {

// Forward declarations
class VPIDBridge;
class ModelManager;

/**
 * @brief Result of KV cache extraction
 */
struct KVExtractionResult {
    bool success = false;
    std::string error_message;

    // Extracted data
    std::vector<uint8_t> kv_state;      ///< Raw llama.cpp state data
    uint32_t token_count = 0;           ///< Number of tokens processed
    int32_t sequence_id = 0;            ///< Sequence ID used

    // Timing
    double tokenize_time_ms = 0.0;
    double prefill_time_ms = 0.0;
    double extract_time_ms = 0.0;
    double total_time_ms = 0.0;
};

/**
 * @brief Result of KV cache injection
 */
struct KVInjectionResult {
    bool success = false;
    std::string error_message;

    uint32_t tokens_restored = 0;
    double inject_time_ms = 0.0;

    // Context with injected KV cache (caller must manage lifecycle)
    llama_context* ctx = nullptr;
};

/**
 * @brief Configuration for KV extraction
 */
struct KVExtractionConfig {
    int32_t sequence_id = 0;            ///< Sequence ID to use (default: 0)
    bool include_logits = false;        ///< Include logits in state (default: false)
    int batch_size = 512;               ///< Tokens per batch during prefill
    bool verbose = false;               ///< Print progress

    KVExtractionConfig() = default;
};

/**
 * @brief KV Cache Extractor
 *
 * Extracts KV cache from llama.cpp inference context and converts
 * to our persistent format for vPID L2.
 *
 * Usage:
 * @code
 * KVCacheExtractor extractor(bridge);
 *
 * // Extract KV cache for a document
 * auto result = extractor.extract("model_name", "Document text here...");
 * if (result.success) {
 *     // Save result.kv_state to disk
 * }
 *
 * // Later, restore KV cache for queries
 * extractor.inject("model_name", kv_state);
 * @endcode
 */
class KVCacheExtractor {
public:
    /**
     * @brief Construct extractor with VPIDBridge
     * @param bridge Pointer to VPIDBridge for model access
     */
    explicit KVCacheExtractor(VPIDBridge* bridge);

    /**
     * @brief Construct extractor with ModelManager
     * @param manager Pointer to ModelManager
     */
    explicit KVCacheExtractor(ModelManager* manager);

    ~KVCacheExtractor();

    // Prevent copying
    KVCacheExtractor(const KVCacheExtractor&) = delete;
    KVCacheExtractor& operator=(const KVCacheExtractor&) = delete;

    /**
     * @brief Extract KV cache for text content
     *
     * Tokenizes the content, runs prefill forward pass, and extracts
     * the resulting KV cache state.
     *
     * @param model_name Model to use for extraction
     * @param content Text content to process
     * @param config Extraction configuration
     * @return Extraction result with KV state data
     */
    KVExtractionResult extract(
        const std::string& model_name,
        const std::string& content,
        const KVExtractionConfig& config = KVExtractionConfig{}
    );

    /**
     * @brief Extract KV cache from pre-tokenized input
     *
     * @param model_name Model to use
     * @param tokens Pre-tokenized input
     * @param config Extraction configuration
     * @return Extraction result
     */
    KVExtractionResult extract_from_tokens(
        const std::string& model_name,
        const std::vector<int32_t>& tokens,
        const KVExtractionConfig& config = KVExtractionConfig{}
    );

    /**
     * @brief Inject KV cache state into context
     *
     * Restores previously extracted KV cache, enabling O(1) query
     * access to the cached context.
     *
     * @param model_name Model to inject into
     * @param kv_state Raw KV state data (from extract())
     * @param sequence_id Target sequence ID
     * @return Injection result
     */
    KVInjectionResult inject(
        const std::string& model_name,
        const std::vector<uint8_t>& kv_state,
        int32_t sequence_id = 0
    );

    /**
     * @brief Clear KV cache for a sequence
     *
     * @param model_name Model name
     * @param sequence_id Sequence to clear (-1 for all)
     * @return true if successful
     */
    bool clear_sequence(const std::string& model_name, int32_t sequence_id = -1);

    /**
     * @brief Get KV cache state size for current context
     *
     * @param model_name Model name
     * @param sequence_id Sequence ID (-1 for full state)
     * @return Size in bytes, 0 on error
     */
    size_t get_state_size(const std::string& model_name, int32_t sequence_id = -1);

    /**
     * @brief Convert raw llama.cpp state to our KVCache format
     *
     * Parses the llama.cpp state buffer and converts to our
     * per-layer K/V tensor format for custom storage.
     *
     * @param model_name Model this state is for
     * @param kv_state Raw llama.cpp state data
     * @param token_count Number of tokens in the state
     * @return KVCache structure, or empty cache on error
     */
    KVCache convert_to_kv_cache(
        const std::string& model_name,
        const std::vector<uint8_t>& kv_state,
        uint32_t token_count
    );

    /**
     * @brief Convert our KVCache format to llama.cpp state
     *
     * @param cache KVCache to convert
     * @return Raw llama.cpp state data
     */
    std::vector<uint8_t> convert_from_kv_cache(const KVCache& cache);

    /**
     * @brief Tokenize text using model's tokenizer
     *
     * @param model_name Model to use for tokenization
     * @param text Text to tokenize
     * @param add_bos Add beginning-of-sequence token
     * @return Vector of token IDs
     */
    std::vector<int32_t> tokenize(
        const std::string& model_name,
        const std::string& text,
        bool add_bos = true
    );

    /**
     * @brief Get model's KV cache shape parameters
     *
     * @param model_name Model name
     * @return KVCacheShape with model parameters
     */
    KVCacheShape get_model_kv_shape(const std::string& model_name);

    /**
     * @brief Check if model supports KV cache extraction
     *
     * @param model_name Model name
     * @return true if extraction is supported
     */
    bool supports_extraction(const std::string& model_name);

    /**
     * @brief Clear cached context for a model
     *
     * Frees the cached llama_context for the specified model.
     * Use this when switching models or to force context recreation.
     *
     * @param model_name Model name to clear cache for (empty = clear all)
     */
    void clear_context_cache(const std::string& model_name = "");

private:
    VPIDBridge* bridge_;
    ModelManager* manager_;

    // Context cache - stores one context per model to avoid memory leaks
    // Previously, get_context() created a new context every call without freeing
    mutable std::mutex context_cache_mutex_;
    std::unordered_map<std::string, llama_context*> cached_contexts_;

    // Internal helpers
    llama_context* get_context(const std::string& model_name);
    llama_model* get_model(const std::string& model_name);

    // Run prefill in batches
    bool run_prefill(
        llama_context* ctx,
        const std::vector<int32_t>& tokens,
        int32_t sequence_id,
        int batch_size,
        bool verbose
    );
};

/**
 * @brief Progress callback for long extractions
 */
using ExtractionProgressCallback = std::function<void(
    int tokens_processed,
    int total_tokens,
    double elapsed_ms
)>;

/**
 * @brief Extended extraction with progress callback
 */
KVExtractionResult extract_with_progress(
    KVCacheExtractor& extractor,
    const std::string& model_name,
    const std::string& content,
    ExtractionProgressCallback callback,
    const KVExtractionConfig& config = KVExtractionConfig{}
);

} // namespace snapllm
