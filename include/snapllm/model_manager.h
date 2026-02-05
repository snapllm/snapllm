/**
 * @file model_manager.h
 * @brief Model Manager - Multi-model orchestration and switching
 *
 * SnapLLM Model Manager provides:
 * - Ultra-fast model switching (<1ms) via vPID architecture
 * - Multi-model support with shared HOT cache
 * - GPU/CPU inference with llama.cpp backend
 * - Batch inference support
 */

#pragma once

#include "vpid_workspace.h"
#include "dequant_cache.h"
#include "vpid_bridge.h"
#include <string>
#include <memory>
#include <vector>
#include <optional>
#include <unordered_set>
#include <unordered_map>

namespace snapllm {

/**
 * @brief Domain type for cache optimization
 */
enum class DomainType {
    General = 0,    // Default balanced configuration
    Code,           // Code generation - large caches
    Chat,           // Conversational - balanced
    Reasoning,      // Complex reasoning - large processing cache
    Vision          // Vision tasks - minimal caching
};

/**
 * @brief Model Manager
 *
 * Orchestrates multiple models, handles switching, and manages resources.
 * Implements the "<1ms model switch" innovation using vPID architecture.
 */
class ModelManager {
public:
    /**
     * @brief Construct with workspace root directory
     * @param workspace_root Path for model workspaces (default: ~/SnapLLM_Workspace)
     *                       Windows: %USERPROFILE%\SnapLLM_Workspace
     *                       Linux: $HOME/SnapLLM_Workspace
     */
    explicit ModelManager(const std::string& workspace_root = "");

    /**
     * @brief Construct with existing VPIDWorkspace (legacy)
     */
    explicit ModelManager(std::shared_ptr<VPIDWorkspace> vpid);

    // Model lifecycle
    bool load_model(const std::string& name, const std::string& gguf_path,
                    bool cache_only = false, DomainType domain = DomainType::General,
                    const GPUConfig& gpu_config = GPUConfig::auto_detect());
    void unload_model(const std::string& name);
    bool switch_model(const std::string& name);

    // Inference
    std::string generate(const std::string& prompt, size_t max_tokens = 100, size_t* actual_tokens = nullptr,
                        float temperature = 0.8f, float top_p = 0.95f, int top_k = 40, float repeat_penalty = 1.1f);
    std::vector<std::string> generate_batch(const std::vector<std::string>& prompts, size_t max_tokens = 100);

    // Streaming inference - true token-by-token streaming with callback
    size_t generate_streaming(const std::string& prompt, TokenCallback callback,
                              size_t max_tokens = 100, float temperature = 0.8f,
                              float top_p = 0.95f, int top_k = 40, float repeat_penalty = 1.1f);

    // Cache-only inference (no GGUF needed after first load)
    std::string run_inference_from_cache(const std::string& model_name,
                                         const std::string& prompt, int max_tokens);

    // Status
    std::string get_current_model() const;
    std::vector<std::string> get_loaded_models() const;
    std::vector<std::string> list_models() const { return get_loaded_models(); }  // MCB alias
    bool is_loaded(const std::string& name) const;
    bool unload_model_bool(const std::string& name);  // Returns bool version for MCB

    // Model info (for MCB integration)
    struct ModelInfo {
        std::string id;
        std::string path;
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

    std::optional<ModelInfo> get_model_info(const std::string& name) const;
    uint32_t get_vpid(const std::string& name) const;

    // Memory stats (for MCB)
    size_t get_gpu_memory_used() const;
    size_t get_gpu_memory_total() const;

    // Validation control
    void enable_validation(bool enabled);
    void set_validation_config(const ValidationConfig& config);
    const ValidationConfig& get_validation_config() const;

    // Cache management
    void print_cache_stats() const;
    void clear_prompt_cache();
    void enable_prompt_cache(bool enabled);

    // Workspace access
    std::shared_ptr<VPIDWorkspace> get_workspace(const std::string& model_name) const;

    // Bridge access (for vPID L2 KV cache extraction)
    std::shared_ptr<VPIDBridge> get_bridge() const { return bridge_; }

private:
    std::string workspace_root_;
    std::shared_ptr<VPIDWorkspace> vpid_;
    std::shared_ptr<DequantCache> cache_;
    std::shared_ptr<VPIDBridge> bridge_;
    std::string current_model_;
    std::unordered_set<std::string> loaded_models_;
    std::unordered_map<std::string, std::string> model_paths_;  // model_name -> gguf_path for auto-reload
    bool prompt_cache_enabled_ = true;

    // Auto-reload evicted model from disk cache
    bool ensure_model_in_gpu(const std::string& name);
};

} // namespace snapllm
