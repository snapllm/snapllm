/**
 * @file kv_cache_extractor.cpp
 * @brief KV Cache Extraction Implementation
 *
 * Integrates with llama.cpp's state serialization API for vPID L2.
 */

#include "snapllm/kv_cache_extractor.h"
#include "snapllm/vpid_bridge.h"
#include "snapllm/model_manager.h"

// llama.cpp headers
#include "llama.h"

#include <iostream>
#include <chrono>
#include <cstring>
#include <mutex>

namespace snapllm {

//=============================================================================
// Constructor / Destructor
//=============================================================================

KVCacheExtractor::KVCacheExtractor(VPIDBridge* bridge)
    : bridge_(bridge)
    , manager_(nullptr)
{
    if (!bridge_) {
        std::cerr << "[KVCacheExtractor] Warning: null VPIDBridge provided" << std::endl;
    }
}

KVCacheExtractor::KVCacheExtractor(ModelManager* manager)
    : bridge_(nullptr)
    , manager_(manager)
{
    if (!manager_) {
        std::cerr << "[KVCacheExtractor] Warning: null ModelManager provided" << std::endl;
    }
}

KVCacheExtractor::~KVCacheExtractor() {
    // Clean up all cached contexts to prevent memory leaks
    std::lock_guard<std::mutex> lock(context_cache_mutex_);
    for (auto& pair : cached_contexts_) {
        if (pair.second) {
            llama_free(pair.second);
        }
    }
    cached_contexts_.clear();
}

//=============================================================================
// Context/Model Access
//=============================================================================

llama_context* KVCacheExtractor::get_context(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(context_cache_mutex_);

    // Check cache first
    auto it = cached_contexts_.find(model_name);
    if (it != cached_contexts_.end() && it->second != nullptr) {
        return it->second;
    }

    // Create new context
    llama_context* ctx = nullptr;

    if (bridge_) {
        // Get context from VPIDBridge directly
        ctx = bridge_->create_inference_context(model_name);
    } else if (manager_) {
        // Get context via ModelManager's bridge
        auto bridge = manager_->get_bridge();
        if (bridge) {
            ctx = bridge->create_inference_context(model_name);
        }
    }

    // Cache the context for reuse
    if (ctx) {
        cached_contexts_[model_name] = ctx;
    }

    return ctx;
}

void KVCacheExtractor::clear_context_cache(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(context_cache_mutex_);

    if (model_name.empty()) {
        // Clear all cached contexts
        for (auto& pair : cached_contexts_) {
            if (pair.second) {
                llama_free(pair.second);
            }
        }
        cached_contexts_.clear();
    } else {
        // Clear specific model's context
        auto it = cached_contexts_.find(model_name);
        if (it != cached_contexts_.end()) {
            if (it->second) {
                llama_free(it->second);
            }
            cached_contexts_.erase(it);
        }
    }
}

llama_model* KVCacheExtractor::get_model(const std::string& model_name) {
    // The model is accessed through the context
    // llama.cpp doesn't expose direct model access easily
    return nullptr;
}

//=============================================================================
// Tokenization
//=============================================================================

std::vector<int32_t> KVCacheExtractor::tokenize(
    const std::string& model_name,
    const std::string& text,
    bool add_bos
) {
    std::vector<int32_t> tokens;

    llama_context* ctx = get_context(model_name);
    if (!ctx) {
        std::cerr << "[KVCacheExtractor] Failed to get context for tokenization" << std::endl;
        return tokens;
    }

    const llama_model* model = llama_get_model(ctx);
    if (!model) {
        std::cerr << "[KVCacheExtractor] Failed to get model for tokenization" << std::endl;
        return tokens;
    }

    // Get vocabulary info
    const llama_vocab* vocab = llama_model_get_vocab(model);
    if (!vocab) {
        return tokens;
    }

    // Estimate token count (roughly 1 token per 4 chars, with buffer)
    int max_tokens = static_cast<int>(text.length() / 2) + 128;
    tokens.resize(max_tokens);

    // Tokenize
    int n_tokens = llama_tokenize(
        vocab,
        text.c_str(),
        static_cast<int32_t>(text.length()),
        tokens.data(),
        max_tokens,
        add_bos,
        true  // special tokens
    );

    if (n_tokens < 0) {
        // Buffer too small, resize and retry
        tokens.resize(-n_tokens);
        n_tokens = llama_tokenize(
            vocab,
            text.c_str(),
            static_cast<int32_t>(text.length()),
            tokens.data(),
            static_cast<int32_t>(tokens.size()),
            add_bos,
            true
        );
    }

    if (n_tokens > 0) {
        tokens.resize(n_tokens);
    } else {
        tokens.clear();
    }

    return tokens;
}

//=============================================================================
// Model KV Shape
//=============================================================================

KVCacheShape KVCacheExtractor::get_model_kv_shape(const std::string& model_name) {
    KVCacheShape shape;

    llama_context* ctx = get_context(model_name);
    if (!ctx) {
        return shape;
    }

    const llama_model* model = llama_get_model(ctx);
    if (!model) {
        return shape;
    }

    // Get model hyperparameters
    shape.num_layers = llama_model_n_layer(model);
    shape.num_heads = llama_model_n_head(model);
    shape.head_dim = llama_model_n_embd(model) / shape.num_heads;
    shape.sequence_length = 0;  // Will be set during extraction
    shape.dtype = KVDataType::FP16;  // Default, can be configured

    return shape;
}

bool KVCacheExtractor::supports_extraction(const std::string& model_name) {
    llama_context* ctx = get_context(model_name);
    return ctx != nullptr;
}

//=============================================================================
// Prefill Execution
//=============================================================================

bool KVCacheExtractor::run_prefill(
    llama_context* ctx,
    const std::vector<int32_t>& tokens,
    int32_t sequence_id,
    int batch_size,
    bool verbose
) {
    if (!ctx || tokens.empty()) {
        return false;
    }

    // Process tokens in batches
    const int n_tokens = static_cast<int>(tokens.size());
    int n_processed = 0;

    while (n_processed < n_tokens) {
        int n_batch = std::min(batch_size, n_tokens - n_processed);

        // Create batch - llama_batch_init allocates arrays for us
        // Parameters: n_tokens, embd (0 = use tokens), n_seq_max (max sequences per token)
        llama_batch batch = llama_batch_init(n_batch, 0, 1);
        batch.n_tokens = n_batch;

        // Manually populate the batch (replacing llama_batch_add)
        for (int i = 0; i < n_batch; i++) {
            int pos = n_processed + i;
            batch.token[i] = tokens[pos];
            batch.pos[i] = pos;
            batch.n_seq_id[i] = 1;
            batch.seq_id[i][0] = sequence_id;
            batch.logits[i] = 0;  // Don't compute logits for this token
        }

        // Set logits for last token of final batch
        if (n_processed + n_batch >= n_tokens) {
            batch.logits[n_batch - 1] = 1;
        }

        // Decode batch
        int result = llama_decode(ctx, batch);

        llama_batch_free(batch);

        if (result != 0) {
            std::cerr << "[KVCacheExtractor] llama_decode failed at position "
                      << n_processed << std::endl;
            return false;
        }

        n_processed += n_batch;

        if (verbose && n_tokens > 1000) {
            std::cout << "\r[KVCacheExtractor] Prefill: "
                      << n_processed << "/" << n_tokens << " tokens"
                      << std::flush;
        }
    }

    if (verbose && n_tokens > 1000) {
        std::cout << std::endl;
    }

    return true;
}

//=============================================================================
// KV Cache Extraction
//=============================================================================

KVExtractionResult KVCacheExtractor::extract(
    const std::string& model_name,
    const std::string& content,
    const KVExtractionConfig& config
) {
    KVExtractionResult result;
    auto total_start = std::chrono::high_resolution_clock::now();

    // Tokenize
    auto token_start = std::chrono::high_resolution_clock::now();
    std::vector<int32_t> tokens = tokenize(model_name, content, true);
    auto token_end = std::chrono::high_resolution_clock::now();
    result.tokenize_time_ms = std::chrono::duration<double, std::milli>(token_end - token_start).count();

    if (tokens.empty()) {
        result.error_message = "Tokenization failed";
        return result;
    }

    if (config.verbose) {
        std::cout << "[KVCacheExtractor] Tokenized " << content.size()
                  << " chars to " << tokens.size() << " tokens" << std::endl;
    }

    // Extract from tokens
    return extract_from_tokens(model_name, tokens, config);
}

KVExtractionResult KVCacheExtractor::extract_from_tokens(
    const std::string& model_name,
    const std::vector<int32_t>& tokens,
    const KVExtractionConfig& config
) {
    KVExtractionResult result;
    auto total_start = std::chrono::high_resolution_clock::now();

    // Get context
    llama_context* ctx = get_context(model_name);
    if (!ctx) {
        result.error_message = "Failed to get inference context for model: " + model_name;
        return result;
    }

    // Clear any existing KV cache for this sequence
    llama_memory_seq_rm(llama_get_memory(ctx), config.sequence_id, -1, -1);

    // Run prefill
    auto prefill_start = std::chrono::high_resolution_clock::now();
    bool prefill_ok = run_prefill(
        ctx, tokens, config.sequence_id, config.batch_size, config.verbose
    );
    auto prefill_end = std::chrono::high_resolution_clock::now();
    result.prefill_time_ms = std::chrono::duration<double, std::milli>(prefill_end - prefill_start).count();

    if (!prefill_ok) {
        result.error_message = "Prefill failed";
        return result;
    }

    // Extract KV state
    auto extract_start = std::chrono::high_resolution_clock::now();

    // Get state size for this sequence
    size_t state_size = llama_state_seq_get_size(ctx, config.sequence_id);
    if (state_size == 0) {
        result.error_message = "Failed to get KV state size";
        return result;
    }

    if (config.verbose) {
        std::cout << "[KVCacheExtractor] KV state size: "
                  << (state_size / (1024.0 * 1024.0)) << " MB" << std::endl;
    }

    // Allocate buffer and extract
    result.kv_state.resize(state_size);
    size_t bytes_copied = llama_state_seq_get_data(
        ctx,
        result.kv_state.data(),
        state_size,
        config.sequence_id
    );

    auto extract_end = std::chrono::high_resolution_clock::now();
    result.extract_time_ms = std::chrono::duration<double, std::milli>(extract_end - extract_start).count();

    if (bytes_copied != state_size) {
        result.error_message = "State extraction size mismatch";
        result.kv_state.clear();
        return result;
    }

    // Success
    auto total_end = std::chrono::high_resolution_clock::now();
    result.success = true;
    result.token_count = static_cast<uint32_t>(tokens.size());
    result.sequence_id = config.sequence_id;
    result.total_time_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();

    if (config.verbose) {
        std::cout << "[KVCacheExtractor] Extraction complete:" << std::endl;
        std::cout << "  Tokens: " << result.token_count << std::endl;
        std::cout << "  State size: " << (state_size / (1024.0 * 1024.0)) << " MB" << std::endl;
        std::cout << "  Tokenize: " << result.tokenize_time_ms << " ms" << std::endl;
        std::cout << "  Prefill: " << result.prefill_time_ms << " ms" << std::endl;
        std::cout << "  Extract: " << result.extract_time_ms << " ms" << std::endl;
        std::cout << "  Total: " << result.total_time_ms << " ms" << std::endl;
    }

    return result;
}

//=============================================================================
// KV Cache Injection
//=============================================================================

KVInjectionResult KVCacheExtractor::inject(
    const std::string& model_name,
    const std::vector<uint8_t>& kv_state,
    int32_t sequence_id
) {
    KVInjectionResult result;
    auto start = std::chrono::high_resolution_clock::now();

    if (kv_state.empty()) {
        result.error_message = "Empty KV state";
        return result;
    }

    // Get context
    llama_context* ctx = get_context(model_name);
    if (!ctx) {
        result.error_message = "Failed to get inference context for model: " + model_name;
        return result;
    }

    // Clear existing sequence
    llama_memory_seq_rm(llama_get_memory(ctx), sequence_id, -1, -1);

    // Restore KV state
    size_t bytes_set = llama_state_seq_set_data(
        ctx,
        kv_state.data(),
        kv_state.size(),
        sequence_id
    );

    auto end = std::chrono::high_resolution_clock::now();
    result.inject_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    if (bytes_set != kv_state.size()) {
        result.error_message = "State injection size mismatch";
        return result;
    }

    result.success = true;
    result.ctx = ctx;  // Return context for generation with injected KV cache
    // Note: tokens_restored would need to be parsed from the state header
    // For now, we leave it at 0

    return result;
}

//=============================================================================
// Sequence Management
//=============================================================================

bool KVCacheExtractor::clear_sequence(const std::string& model_name, int32_t sequence_id) {
    llama_context* ctx = get_context(model_name);
    if (!ctx) {
        return false;
    }

    if (sequence_id < 0) {
        // Clear all sequences
        llama_memory_clear(llama_get_memory(ctx), true);
    } else {
        // Clear specific sequence
        llama_memory_seq_rm(llama_get_memory(ctx), sequence_id, -1, -1);
    }

    return true;
}

size_t KVCacheExtractor::get_state_size(const std::string& model_name, int32_t sequence_id) {
    llama_context* ctx = get_context(model_name);
    if (!ctx) {
        return 0;
    }

    if (sequence_id < 0) {
        // Full state size
        return llama_state_get_size(ctx);
    } else {
        // Per-sequence size
        return llama_state_seq_get_size(ctx, sequence_id);
    }
}

//=============================================================================
// Format Conversion
//=============================================================================

KVCache KVCacheExtractor::convert_to_kv_cache(
    const std::string& model_name,
    const std::vector<uint8_t>& kv_state,
    uint32_t token_count
) {
    KVCache cache;

    // Get model shape
    KVCacheShape shape = get_model_kv_shape(model_name);
    // Set sequence_length BEFORE validation (it's passed as parameter)
    shape.sequence_length = token_count;

    if (!shape.is_valid()) {
        std::cerr << "[KVCacheExtractor] Invalid model shape: "
                  << "layers=" << shape.num_layers
                  << ", heads=" << shape.num_heads
                  << ", head_dim=" << shape.head_dim
                  << ", seq_len=" << shape.sequence_length << std::endl;
        return cache;
    }
    cache.shape = shape;
    cache.model_id = model_name;

    // For now, store the raw llama.cpp state directly
    // A more sophisticated implementation would parse the state
    // and extract individual K/V tensors per layer
    //
    // The llama.cpp state format is:
    // [metadata header]
    // [cell positions and sequences per layer]
    // [K tensor data per layer]
    // [V tensor data per layer]
    //
    // For vPID L2, we can either:
    // 1. Store raw state (simpler, works with llama.cpp restore)
    // 2. Parse and store per-layer (more flexible, custom format)
    //
    // We choose option 1 for compatibility

    // Store entire state as a single "layer" for now
    cache.layers.resize(1);
    cache.layers[0].keys = kv_state;  // Store raw state in keys
    cache.layers[0].values.clear();   // Empty values (state includes both)

    return cache;
}

std::vector<uint8_t> KVCacheExtractor::convert_from_kv_cache(const KVCache& cache) {
    // For raw state storage, just return the data from the first layer
    if (!cache.layers.empty() && !cache.layers[0].keys.empty()) {
        return cache.layers[0].keys;
    }
    return {};
}

//=============================================================================
// Progress Callback Support
//=============================================================================

KVExtractionResult extract_with_progress(
    KVCacheExtractor& extractor,
    const std::string& model_name,
    const std::string& content,
    ExtractionProgressCallback callback,
    const KVExtractionConfig& config
) {
    // Tokenize first
    std::vector<int32_t> tokens = extractor.tokenize(model_name, content, true);
    if (tokens.empty()) {
        KVExtractionResult result;
        result.error_message = "Tokenization failed";
        return result;
    }

    // Report tokenization complete
    if (callback) {
        callback(0, static_cast<int>(tokens.size()), 0);
    }

    // TODO: Modify extract_from_tokens to accept progress callback
    // For now, just call the standard extraction
    return extractor.extract_from_tokens(model_name, tokens, config);
}

} // namespace snapllm
