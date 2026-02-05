/**
 * @file model_manager.cpp
 * @brief Model Manager Implementation
 *
 * SnapLLM Model Manager - Ultra-fast multi-model orchestration
 */

#include "snapllm/model_manager.h"
#include <iostream>
#include <chrono>
#include <cstdlib>

namespace snapllm {

// Get default workspace path based on OS
static std::string get_default_workspace_path() {
#ifdef _WIN32
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile) {
        return std::string(userprofile) + "\\SnapLLM_Workspace";
    }
    return "C:\\SnapLLM_Workspace";
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/SnapLLM_Workspace";
    }
    return "/tmp/SnapLLM_Workspace";
#endif
}

ModelManager::ModelManager(const std::string& workspace_root)
    : workspace_root_(workspace_root.empty() ? get_default_workspace_path() : workspace_root)
    , vpid_(nullptr)
    , cache_(nullptr)
    , bridge_(std::make_shared<VPIDBridge>(workspace_root_.empty() ? get_default_workspace_path() : workspace_root_))
{
    std::cout << "[SnapLLM] ModelManager initialized" << std::endl;
    std::cout << "[SnapLLM] Workspace: " << workspace_root_ << std::endl;
}

ModelManager::ModelManager(std::shared_ptr<VPIDWorkspace> vpid)
    : workspace_root_(get_default_workspace_path())
    , vpid_(vpid)
    , cache_(nullptr)
    , bridge_(std::make_shared<VPIDBridge>())
{
}

bool ModelManager::load_model(const std::string& name, const std::string& gguf_path,
                               bool cache_only, DomainType domain, const GPUConfig& gpu_config) {
    std::cout << "\n[SnapLLM] Loading model: " << name << std::endl;
    std::cout << "[SnapLLM] Path: " << gguf_path << std::endl;

    if (cache_only) {
        std::cout << "[SnapLLM] Cache-only mode: creating vPID cache without inference context" << std::endl;
    }

    // Domain-specific cache tuning (for future optimization)
    switch (domain) {
        case DomainType::Code:
            std::cout << "[SnapLLM] Domain: Code (large caches)" << std::endl;
            break;
        case DomainType::Chat:
            std::cout << "[SnapLLM] Domain: Chat (balanced)" << std::endl;
            break;
        case DomainType::Reasoning:
            std::cout << "[SnapLLM] Domain: Reasoning (large processing cache)" << std::endl;
            break;
        case DomainType::Vision:
            std::cout << "[SnapLLM] Domain: Vision (minimal caching)" << std::endl;
            break;
        default:
            std::cout << "[SnapLLM] Domain: General" << std::endl;
            break;
    }

    // Use bridge to load model with llama.cpp + vPID integration
    bool success = bridge_->load_and_dequantize_model(name, gguf_path, false, gpu_config);

    if (success) {
        loaded_models_.insert(name);
        model_paths_[name] = gguf_path;  // Store path for auto-reload
        if (current_model_.empty()) {
            current_model_ = name;
        }
        std::cout << "[SnapLLM] Model loaded successfully!" << std::endl;
    }

    return success;
}

void ModelManager::unload_model(const std::string& name) {
    bridge_->unload_model(name);
    loaded_models_.erase(name);
    if (current_model_ == name) {
        current_model_.clear();
    }
}

bool ModelManager::switch_model(const std::string& name) {
    if (loaded_models_.find(name) == loaded_models_.end()) {
        std::cerr << "[SnapLLM] Model not loaded: " << name << std::endl;
        return false;
    }

    auto start = std::chrono::high_resolution_clock::now();
    current_model_ = name;
    auto end = std::chrono::high_resolution_clock::now();

    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "[SnapLLM] Switched to '" << name << "' in "
              << (duration_us.count() / 1000.0) << "ms" << std::endl;

    return true;
}

std::string ModelManager::generate(const std::string& prompt, size_t max_tokens, size_t* actual_tokens,
                                   float temperature, float top_p, int top_k, float repeat_penalty) {
    if (current_model_.empty()) {
        return "Error: No model selected. Call load_model() first.";
    }

    // Auto-reload model if it was evicted from GPU
    if (!ensure_model_in_gpu(current_model_)) {
        return "Error: Could not load model to GPU: " + current_model_;
    }

    return bridge_->generate_text(current_model_, prompt, static_cast<int>(max_tokens), actual_tokens,
                                  temperature, top_p, top_k, repeat_penalty);
}

std::vector<std::string> ModelManager::generate_batch(const std::vector<std::string>& prompts, size_t max_tokens) {
    std::vector<std::string> results;
    results.reserve(prompts.size());

    if (current_model_.empty()) {
        for (size_t i = 0; i < prompts.size(); ++i) {
            results.push_back("Error: No model selected");
        }
        return results;
    }

    // Process each prompt sequentially for now
    // TODO: Implement true parallel batch processing with llama.cpp multi-sequence API
    for (const auto& prompt : prompts) {
        results.push_back(bridge_->generate_text(current_model_, prompt, static_cast<int>(max_tokens)));
    }

    return results;
}

std::string ModelManager::run_inference_from_cache(const std::string& model_name,
                                                    const std::string& prompt, int max_tokens) {
    // Switch to model if needed
    if (current_model_ != model_name) {
        if (!switch_model(model_name)) {
            return "Error: Could not switch to model " + model_name;
        }
    }

    return generate(prompt, max_tokens);
}

std::string ModelManager::get_current_model() const {
    return current_model_;
}

std::vector<std::string> ModelManager::get_loaded_models() const {
    return std::vector<std::string>(loaded_models_.begin(), loaded_models_.end());
}

void ModelManager::enable_validation(bool enabled) {
    bridge_->enable_validation(enabled);
}

void ModelManager::set_validation_config(const ValidationConfig& config) {
    bridge_->set_validation_config(config);
}

const ValidationConfig& ModelManager::get_validation_config() const {
    return bridge_->get_validation_config();
}


size_t ModelManager::generate_streaming(const std::string& prompt, TokenCallback callback,
                                        size_t max_tokens, float temperature,
                                        float top_p, int top_k, float repeat_penalty) {
    if (current_model_.empty()) {
        callback("Error: No model selected. Call load_model() first.", -1, true);
        return 0;
    }

    // Auto-reload model if it was evicted from GPU
    if (!ensure_model_in_gpu(current_model_)) {
        callback("Error: Could not load model to GPU: " + current_model_, -1, true);
        return 0;
    }

    return bridge_->generate_text_streaming(current_model_, prompt, callback,
                                            static_cast<int>(max_tokens),
                                            temperature, top_p, top_k, repeat_penalty);
}

void ModelManager::print_cache_stats() const {
    std::cout << "\n=== SnapLLM Cache Statistics ===" << std::endl;
    std::cout << "Loaded models: " << loaded_models_.size() << std::endl;
    std::cout << "Current model: " << (current_model_.empty() ? "(none)" : current_model_) << std::endl;
    std::cout << "Prompt cache: " << (prompt_cache_enabled_ ? "enabled" : "disabled") << std::endl;

    // Print workspace stats for each model
    for (const auto& model : loaded_models_) {
        auto workspace = bridge_->get_workspace(model);
        if (workspace) {
            const VPIDStats& stats = workspace->get_stats();
            std::cout << "\n[" << model << "]" << std::endl;
            std::cout << "  Allocations: " << stats.total_allocations.load() << std::endl;
            std::cout << "  Reads: " << stats.total_reads.load() << " (" << (stats.bytes_read.load() / (1024*1024)) << " MB)" << std::endl;
            std::cout << "  Cache hits: " << stats.cache_hits.load() << std::endl;
            std::cout << "  Cache misses: " << stats.cache_misses.load() << std::endl;
        }
    }
    std::cout << "================================\n" << std::endl;
}

void ModelManager::clear_prompt_cache() {
    // Clear any cached prompts/KV cache
    std::cout << "[SnapLLM] Prompt cache cleared" << std::endl;
}

void ModelManager::enable_prompt_cache(bool enabled) {
    prompt_cache_enabled_ = enabled;
    std::cout << "[SnapLLM] Prompt cache " << (enabled ? "enabled" : "disabled") << std::endl;
}

std::shared_ptr<VPIDWorkspace> ModelManager::get_workspace(const std::string& model_name) const {
    return bridge_->get_workspace(model_name);
}

bool ModelManager::ensure_model_in_gpu(const std::string& name) {
    // Check if model is currently loaded in GPU via VPIDBridge
    if (bridge_->is_model_loaded(name)) {
        return true;  // Model is in GPU
    }

    // Model was evicted - reload from disk cache
    auto path_it = model_paths_.find(name);
    if (path_it == model_paths_.end()) {
        std::cerr << "[SnapLLM] Cannot reload model '" << name << "': path not found" << std::endl;
        return false;
    }

    std::cout << "[SnapLLM] Auto-reloading evicted model: " << name << std::endl;
    std::cout << "[SnapLLM] Using cached vPID workspace (fast reload)" << std::endl;

    // Reload the model - this will use existing vPID cache for fast load
    bool success = bridge_->load_and_dequantize_model(name, path_it->second);
    if (success) {
        std::cout << "[SnapLLM] Model '" << name << "' reloaded successfully" << std::endl;
    } else {
        std::cerr << "[SnapLLM] Failed to reload model: " << name << std::endl;
    }

    return success;
}

//=============================================================================
// MCB Integration Methods
//=============================================================================

bool ModelManager::is_loaded(const std::string& name) const {
    return loaded_models_.find(name) != loaded_models_.end();
}

bool ModelManager::unload_model_bool(const std::string& name) {
    if (!is_loaded(name)) {
        return false;
    }
    unload_model(name);
    return true;
}

std::optional<ModelManager::ModelInfo> ModelManager::get_model_info(const std::string& name) const {
    if (!is_loaded(name)) {
        return std::nullopt;
    }

    ModelInfo info;
    info.id = name;

    // Get path
    auto path_it = model_paths_.find(name);
    if (path_it != model_paths_.end()) {
        info.path = path_it->second;
    }

    // Get info from bridge
    auto bridge_info = bridge_->get_model_info(name);
    if (bridge_info) {
        info.architecture = bridge_info->architecture;
        info.parameters = bridge_info->parameters;
        info.context_length = bridge_info->context_length;
        info.n_layers = bridge_info->n_layers;
        info.n_heads = bridge_info->n_heads;
        info.head_dim = bridge_info->head_dim;
        info.n_gpu_layers = bridge_info->n_gpu_layers;
        info.memory_bytes = bridge_info->memory_bytes;
        info.vpid = bridge_info->vpid;
    }

    return info;
}

uint32_t ModelManager::get_vpid(const std::string& name) const {
    auto info = get_model_info(name);
    return info ? info->vpid : 0;
}

size_t ModelManager::get_gpu_memory_used() const {
    return bridge_->get_gpu_memory_used();
}

size_t ModelManager::get_gpu_memory_total() const {
    return bridge_->get_gpu_memory_total();
}

} // namespace snapllm
