/**
 * @file vpid_bridge.h
 * @brief Bridge between vPID cache and llama.cpp inference
 *
 * This module acts as an adapter between SnapLLM's vPID cache system
 * and llama.cpp's inference engine. It allows llama.cpp to load
 * pre-dequantized F32 tensors directly from the vPID workspace
 * instead of loading and dequantizing from GGUF files.
 */

#pragma once

#include "vpid_workspace.h"
#include "dequant_cache.h"
#include "vpid_hot_cache.h"
#include "validation.h"
#include "workspace_metadata.h"
#include <string>
#include <memory>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <functional>

// Token streaming callback type
// Parameters: token_text, token_id, is_eos (end of sequence)
// Return: true to continue, false to stop generation
using TokenCallback = std::function<bool(const std::string& token, int token_id, bool is_eos)>;

/**
 * @brief GPU configuration for model loading
 */
struct GPUConfig {
    int n_gpu_layers = -1;      // Number of layers to offload to GPU (-1 = auto, 0 = CPU only, 999 = all)
    size_t vram_budget_mb = 0;  // VRAM budget in MB (0 = auto-detect)
    bool use_flash_attn = true; // Enable Flash Attention when available
    
    static GPUConfig auto_detect() {
        GPUConfig config;
        config.n_gpu_layers = -1;  // Auto-detect
        return config;
    }
    
    static GPUConfig cpu_only() {
        GPUConfig config;
        config.n_gpu_layers = 0;
        return config;
    }
    
    static GPUConfig with_layers(int layers) {
        GPUConfig config;
        config.n_gpu_layers = layers;
        return config;
    }
};

// Forward declare llama.cpp types
struct llama_model;
struct llama_context;
struct ggml_tensor;
struct ggml_context;

namespace snapllm {

/**
 * @class VPIDBridge
 * @brief Bridge between vPID cache and llama.cpp
 *
 * This class provides the integration layer that allows llama.cpp
 * to use pre-dequantized tensors from the vPID cache instead of
 * loading quantized weights from GGUF files.
 *
 * Key functionality:
 * - Load GGUF models using llama.cpp's parser
 * - Dequantize all tensors using llama.cpp's optimized kernels
 * - Store dequantized F32 tensors in vPID workspace
 * - Provide tensor data to llama.cpp during inference
 *
 * Usage:
 * @code
 * auto vpid = std::make_shared<VPIDWorkspace>("workspace.bin", 100 * 1024ULL * 1024 * 1024);
 * auto cache = std::make_shared<DequantCache>(vpid);
 * VPIDBridge bridge(cache);
 *
 * // Load model - dequantizes once and stores in vPID
 * bridge.load_model("llama3", "D:\\Models\\llama3-8b-q5.gguf");
 *
 * // Create inference context - uses vPID tensors directly
 * auto ctx = bridge.create_inference_context("llama3");
 * @endcode
 */
class VPIDBridge {
public:
    /**
     * @brief Construct bridge with workspace root directory
     * @param workspace_root Root directory for model workspaces
     *                       Default: ~/SnapLLM_Workspace (user's home directory)
     */
    explicit VPIDBridge(const std::string& workspace_root = "");

    /**
     * @brief Destructor
     */
    ~VPIDBridge();

    /**
     * @brief Load GGUF model, dequantize, and store in vPID
     *
     * This method:
     * 1. Uses llama.cpp's GGUF parser to read model metadata and tensors
     * 2. Iterates through all tensors in the model
     * 3. Uses llama.cpp's optimized dequantization functions (dequantize_row_q4_0, etc.)
     * 4. Stores the resulting F32 tensors in vPID workspace
     * 5. Builds tensor catalog for fast lookup
     *
     * @param model_name Name to identify this model
     * @param gguf_path Path to GGUF file
     * @param force_reload If true, reload even if already in cache
     * @return true if successful, false otherwise
     */
    bool load_and_dequantize_model(
        const std::string& model_name,
        const std::string& gguf_path,
        bool force_reload = false,
        const GPUConfig& gpu_config = GPUConfig::auto_detect()
    );

    /**
     * @brief Create llama.cpp inference context using vPID tensors
     *
     * This creates a llama_context that is configured to use the
     * pre-dequantized F32 tensors from vPID instead of loading
     * from a GGUF file.
     *
     * @param model_name Name of model to use
     * @param n_ctx Context size (default: 2048)
     * @param n_batch Batch size (default: 512)
     * @return Pointer to llama_context (caller must free with llama_free)
     */
    llama_context* create_inference_context(
        const std::string& model_name,
        int n_ctx = 2048,
        int n_batch = 512
    );

    /**
     * @brief Get pointer to tensor data for llama.cpp
     *
     * This is called by llama.cpp during inference to get tensor data.
     * Returns a direct pointer into the vPID mapped memory (zero-copy).
     *
     * @param model_name Model name
     * @param tensor_name Tensor name
     * @return Pointer to F32 data, or nullptr if not found
     */
    const float* get_tensor_data(
        const std::string& model_name,
        const std::string& tensor_name
    );

    /**
     * @brief Get tensor metadata
     * @param model_name Model name
     * @param tensor_name Tensor name
     * @return Pointer to tensor info, or nullptr if not found
     */
    const TensorInfo* get_tensor_info(
        const std::string& model_name,
        const std::string& tensor_name
    );

    /**
     * @brief Unload model from cache
     * @param model_name Model to unload
     */
    void unload_model(const std::string& model_name);

    /**
     * @brief Check if model is loaded
     * @param model_name Model name
     * @return true if loaded
     */
    bool is_model_loaded(const std::string& model_name) const;

    /**
     * @brief Enable/disable validation
     * @param enabled Whether to enable validation
     */
    void enable_validation(bool enabled);

    /**
     * @brief Configure validation settings
     * @param config Validation configuration
     */
    void set_validation_config(const ValidationConfig& config);

    /**
     * @brief Get current validation configuration
     * @return Current validation config
     */
    const ValidationConfig& get_validation_config() const;

    /**
     * @brief Get workspace for a specific model
     * @param model_name Model name
     * @return Workspace pointer, or nullptr if model not loaded
     */
    std::shared_ptr<VPIDWorkspace> get_workspace(const std::string& model_name);

    /**
     * @brief Model information for MCB integration
     */
    struct BridgeModelInfo {
        std::string architecture;
        uint64_t parameters = 0;
        uint32_t context_length = 0;
        uint32_t n_layers = 0;
        uint32_t n_heads = 0;
        uint32_t head_dim = 0;
        uint32_t n_gpu_layers = 0;
        size_t memory_bytes = 0;
        uint32_t vpid = 0;
    };

    /**
     * @brief Get model information
     * @param model_name Model name
     * @return Model info if loaded
     */
    std::optional<BridgeModelInfo> get_model_info(const std::string& model_name) const;

    /**
     * @brief Get GPU memory used
     * @return GPU memory used in bytes
     */
    size_t get_gpu_memory_used() const;

    /**
     * @brief Get total GPU memory
     * @return Total GPU memory in bytes
     */
    size_t get_gpu_memory_total() const;

    /**
     * @brief Generate text from a prompt (Phase 4)
     *
     * This is the end-to-end inference function that:
     * 1. Creates inference context
     * 2. Tokenizes prompt
     * 3. Evaluates tokens
     * 4. Samples and generates new tokens
     * 5. Returns generated text
     *
     * @param model_name Model to use for generation
     * @param prompt Input prompt text
     * @param max_tokens Maximum tokens to generate
     * @return Generated text string
     */
    std::string generate_text(
        const std::string& model_name,
        const std::string& prompt,
        int max_tokens = 50,
        size_t* actual_tokens = nullptr,
        float temperature = 0.8f,
        float top_p = 0.95f,
        int top_k = 40,
        float repeat_penalty = 1.1f
    );

    /**
     * @brief Generate text with streaming callback (true token-by-token streaming)
     *
     * This method enables real-time token streaming by calling the callback
     * function after each token is generated. The callback receives:
     * - token: The decoded text for this token
     * - token_id: The llama.cpp token ID
     * - is_eos: True if this is the end-of-sequence token
     *
     * @param model_name Model to use for generation
     * @param prompt Input prompt text
     * @param callback Function called for each generated token
     * @param max_tokens Maximum tokens to generate
     * @param temperature Sampling temperature (higher = more random)
     * @param top_p Nucleus sampling probability threshold
     * @param top_k Top-k sampling (0 = disabled)
     * @param repeat_penalty Repetition penalty
     * @return Total number of tokens generated
     */
    size_t generate_text_streaming(
        const std::string& model_name,
        const std::string& prompt,
        TokenCallback callback,
        int max_tokens = 50,
        float temperature = 0.8f,
        float top_p = 0.95f,
        int top_k = 40,
        float repeat_penalty = 1.1f
    );

    /**
     * @brief Generate text using pre-injected KV cache (vPID L2)
     *
     * This method generates text after KV cache has been injected via
     * KVCacheExtractor::inject(). It skips the prefill step for cached
     * context tokens and only processes the query tokens.
     *
     * @param model_name Model to use
     * @param ctx Pre-existing context with injected KV cache
     * @param query The query/prompt to process (context already cached)
     * @param context_token_count Number of tokens already in KV cache
     * @param max_tokens Maximum tokens to generate
     * @param temperature Sampling temperature
     * @param top_p Top-p sampling
     * @param top_k Top-k sampling
     * @param repeat_penalty Repetition penalty
     * @return Generated text
     */
    std::string generate_with_injected_kv(
        const std::string& model_name,
        llama_context* ctx,
        const std::string& query,
        int context_token_count,
        int max_tokens = 50,
        float temperature = 0.8f,
        float top_p = 0.95f,
        int top_k = 40,
        float repeat_penalty = 1.1f
    );

    /**
     * @brief Generate text with streaming using pre-injected KV cache (vPID L2)
     *
     * This is the streaming version of generate_with_injected_kv.
     * Skips context prefill by using the already-populated KV cache.
     *
     * @param model_name Name of the model to use
     * @param ctx llama_context with KV cache already injected
     * @param query Query text to generate response for
     * @param context_token_count Number of tokens already in KV cache
     * @param callback Callback function for streaming tokens
     * @param max_tokens Maximum tokens to generate
     * @param temperature Sampling temperature
     * @param top_p Top-p sampling
     * @param top_k Top-k sampling
     * @param repeat_penalty Repetition penalty
     * @return Number of tokens generated
     */
    size_t generate_streaming_with_injected_kv(
        const std::string& model_name,
        llama_context* ctx,
        const std::string& query,
        int context_token_count,
        TokenCallback callback,
        int max_tokens = 50,
        float temperature = 0.8f,
        float top_p = 0.95f,
        int top_k = 40,
        float repeat_penalty = 1.1f
    );

private:
    std::string workspace_root_;  // Root directory for all model workspaces
    std::unique_ptr<VPIDHotCache> hot_cache_;  // HOT tier RAM cache (shared across ALL models)
    TensorValidator validator_;  // Tensor validation system
    std::unique_ptr<WorkspaceMetadata> workspace_metadata_;  // Persistent cache metadata

    // Per-model storage
    std::unordered_map<std::string, std::shared_ptr<DequantCache>> model_caches_;  // model_name -> cache
    std::unordered_map<std::string, llama_model*> loaded_models_;  // model_name -> llama_model
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> model_access_times_;  // For LRU eviction
    mutable std::mutex models_mutex_;  // Mutable to allow locking in const methods

    // GPU memory management - Smart VRAM budgeting
    static constexpr size_t VRAM_BUDGET_MB = 7000;  // RTX 4060 Laptop ~7GB usable VRAM
    std::unordered_map<std::string, size_t> model_vram_usage_;  // model_name -> VRAM in MB
    size_t total_vram_used_ = 0;  // Current total VRAM usage in MB

    /**
     * @brief Evict models until we have enough VRAM space
     * @param needed_mb VRAM space needed in MB
     * @return true if enough space was freed, false otherwise
     */
    bool ensure_vram_space(size_t needed_mb);

    // RAM Cache for FLASH RELOAD - stores evicted model's GGUF path for fast reload
    struct RAMCacheEntry {
        std::string gguf_path;              // Path to GGUF file for reload
        std::string extracted_name;         // Extracted model name
        std::string quant_type;             // Quantization type
        std::chrono::steady_clock::time_point cached_time;
    };
    std::unordered_map<std::string, RAMCacheEntry> ram_cache_;  // model_name -> cache entry

    // Static flag for llama.cpp backend - only init once across all instances
    static bool backend_initialized_;
    static std::mutex backend_mutex_;

    /**
     * @brief Evict least recently used model from GPU memory
     * @return Name of evicted model, or empty string if no eviction needed
     */
    std::string evict_lru_model();

    /**
     * @brief Dequantize a single tensor using llama.cpp functions
     * @param tensor_data Pointer to quantized data
     * @param tensor_type GGML type (Q4_0, Q5_0, etc.)
     * @param num_elements Number of elements
     * @return Vector of dequantized F32 values
     */
    std::vector<float> dequantize_tensor(
        const void* tensor_data,
        int tensor_type,
        size_t num_elements
    );

    /**
     * @brief Load model with llama.cpp structure but wire tensors to vPID
     *
     * This is the custom loader that:
     * 1. Loads GGUF with llama.cpp (gets model structure)
     * 2. Replaces tensor data pointers with vPID pointers
     * 3. Enables fast reload without re-dequantization
     *
     * @param model_name Model name
     * @param gguf_path Path to GGUF file
     * @param model_info Metadata with vPID offsets
     * @param cache DequantCache instance containing the tensors
     * @return true if successful
     */
    bool load_model_with_vpid_tensors(
        const std::string& model_name,
        const std::string& gguf_path,
        const ModelInfo* model_info,
        std::shared_ptr<DequantCache> cache
    );

    /**
     * @brief Get or create DequantCache for a specific model
     *
     * Creates workspace at: D:\SnapLLM_Workspace\<model_name>\<quant_type>\workspace.bin
     * Workspace size is calculated dynamically based on GGUF file size × 2 (overhead factor)
     *
     * @param cache_key Cache lookup key
     * @param workspace_model_name Model name (e.g., "medicine-llm")
     * @param quant_type Quantization type (e.g., "Q8_0")
     * @param gguf_path Path to GGUF file for dynamic size calculation
     * @return Shared pointer to DequantCache, or nullptr on failure
     */
    std::shared_ptr<DequantCache> get_or_create_cache(
        const std::string& cache_key,
        const std::string& workspace_model_name,
        const std::string& quant_type,
        const std::string& gguf_path
    );
};

} // namespace snapllm
