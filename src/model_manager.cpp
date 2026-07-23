/**
 * @file model_manager.cpp
 * @brief Model Manager Implementation
 *
 * SnapLLM Model Manager - multi-model orchestration
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
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    std::cout << "\n[SnapLLM] Loading model: " << name << std::endl;
    std::cout << "[SnapLLM] Path: " << gguf_path << std::endl;

    if (cache_only) {
        std::cerr << "[SnapLLM] Cache-only model loading is not implemented" << std::endl;
        return false;
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
        std::lock_guard<std::mutex> state_lock(state_mutex_);
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
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    bridge_->unload_model(name);
    std::lock_guard<std::mutex> state_lock(state_mutex_);
    loaded_models_.erase(name);
    model_paths_.erase(name);
    if (current_model_ == name) {
        current_model_.clear();
    }
}

bool ModelManager::switch_model(const std::string& name) {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        if (loaded_models_.find(name) == loaded_models_.end()) {
            std::cerr << "[SnapLLM] Model not loaded: " << name << std::endl;
            return false;
        }
    }

    auto start = std::chrono::steady_clock::now();
    if (!ensure_model_in_gpu_locked(name)) {
        std::cerr << "[SnapLLM] Could not make model ready: " << name << std::endl;
        return false;
    }
    {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        current_model_ = name;
    }
    auto end = std::chrono::steady_clock::now();

    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "[SnapLLM] Model '" << name << "' ready in "
              << (duration_us.count() / 1000.0) << "ms" << std::endl;

    return true;
}

std::string ModelManager::generate(const std::string& prompt, size_t max_tokens, size_t* actual_tokens,
                                   float temperature, float top_p, int top_k, float repeat_penalty) {
    const std::string selected_model = get_current_model();
    if (selected_model.empty()) {
        return "Error: No model selected. Call load_model() first.";
    }

    // Auto-reload model if it was evicted from GPU
    if (!ensure_model_in_gpu(selected_model)) {
        return "Error: Could not load model to GPU: " + selected_model;
    }

    return bridge_->generate_text(selected_model, prompt, static_cast<int>(max_tokens), actual_tokens,
                                  temperature, top_p, top_k, repeat_penalty);
}

std::vector<std::string> ModelManager::generate_batch(const std::vector<std::string>& prompts, size_t max_tokens) {
    // Convert to BatchPromptItem and use parallel processing
    std::vector<BatchPromptItem> items;
    items.reserve(prompts.size());
    for (const auto& prompt : prompts) {
        BatchPromptItem item;
        item.raw_prompt = prompt;
        item.max_tokens = static_cast<int>(max_tokens);
        items.push_back(std::move(item));
    }

    auto batch_results = generate_batch(items);

    std::vector<std::string> results;
    results.reserve(prompts.size());
    for (const auto& br : batch_results) {
        results.push_back(br.success ? br.generated_text : ("Error: " + br.error));
    }
    return results;
}

std::vector<BatchResult> ModelManager::generate_batch(
    const std::vector<BatchPromptItem>& items,
    float default_temp, float default_top_p,
    int default_top_k, float default_repeat_penalty)
{
    const std::string selected_model = get_current_model();
    if (selected_model.empty()) {
        std::vector<BatchResult> results(items.size());
        for (auto& r : results) {
            r.success = false;
            r.error = "No model selected";
        }
        return results;
    }

    if (!ensure_model_in_gpu(selected_model)) {
        std::vector<BatchResult> results(items.size());
        for (auto& r : results) {
            r.success = false;
            r.error = "Could not load model to GPU: " + selected_model;
        }
        return results;
    }

    return bridge_->generate_batch_parallel(
        selected_model, items, default_temp, default_top_p, default_top_k, default_repeat_penalty);
}

std::string ModelManager::run_inference_from_cache(const std::string& model_name,
                                                    const std::string& prompt, int max_tokens) {
    (void)model_name;
    (void)prompt;
    (void)max_tokens;
    throw std::logic_error("Cache-only inference is not implemented");
}

std::string ModelManager::get_current_model() const {
    std::lock_guard<std::mutex> state_lock(state_mutex_);
    return current_model_;
}

std::vector<std::string> ModelManager::get_loaded_models() const {
    std::lock_guard<std::mutex> state_lock(state_mutex_);
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
    const std::string selected_model = get_current_model();
    if (selected_model.empty()) {
        callback("Error: No model selected. Call load_model() first.", -1, true);
        return 0;
    }

    // Auto-reload model if it was evicted from GPU
    if (!ensure_model_in_gpu(selected_model)) {
        callback("Error: Could not load model to GPU: " + selected_model, -1, true);
        return 0;
    }

    return bridge_->generate_text_streaming(selected_model, prompt, callback,
                                            static_cast<int>(max_tokens),
                                            temperature, top_p, top_k, repeat_penalty);
}

void ModelManager::print_cache_stats() const {
    std::vector<std::string> models;
    std::string current_model;
    bool prompt_cache_enabled;
    {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        models.assign(loaded_models_.begin(), loaded_models_.end());
        current_model = current_model_;
        prompt_cache_enabled = prompt_cache_enabled_;
    }
    std::cout << "\n=== SnapLLM Cache Statistics ===" << std::endl;
    std::cout << "Loaded models: " << models.size() << std::endl;
    std::cout << "Current model: " << (current_model.empty() ? "(none)" : current_model) << std::endl;
    std::cout << "Prompt cache: " << (prompt_cache_enabled ? "enabled" : "disabled") << std::endl;

    // Print workspace stats for each model
    for (const auto& model : models) {
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
    throw std::logic_error("Prompt-cache clearing is not implemented");
}

void ModelManager::enable_prompt_cache(bool enabled) {
    (void)enabled;
    throw std::logic_error("Prompt-cache control is not implemented");
}

std::shared_ptr<VPIDWorkspace> ModelManager::get_workspace(const std::string& model_name) const {
    return bridge_->get_workspace(model_name);
}

bool ModelManager::ensure_model_in_gpu(const std::string& name) {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    return ensure_model_in_gpu_locked(name);
}

bool ModelManager::ensure_model_in_gpu_locked(const std::string& name) {
    // Check if model is currently loaded in GPU via VPIDBridge
    if (bridge_->is_model_loaded(name)) {
        return true;  // Model is in GPU
    }

    // Model was evicted - reload from disk cache
    std::string model_path;
    {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        auto path_it = model_paths_.find(name);
        if (path_it == model_paths_.end()) {
            std::cerr << "[SnapLLM] Cannot reload model '" << name << "': path not found" << std::endl;
            return false;
        }
        model_path = path_it->second;
    }

    std::cout << "[SnapLLM] Auto-reloading evicted model: " << name << std::endl;
    std::cout << "[SnapLLM] Using cached vPID workspace (fast reload)" << std::endl;

    // Reload the model - mmap + OS page cache makes this fast after first load
    auto reload_start = std::chrono::high_resolution_clock::now();
    bool success = bridge_->load_and_dequantize_model(name, model_path);
    auto reload_end = std::chrono::high_resolution_clock::now();
    auto reload_ms = std::chrono::duration_cast<std::chrono::milliseconds>(reload_end - reload_start).count();

    if (success) {
        std::cout << "[SnapLLM] Reloaded '" << name << "' in " << reload_ms << "ms" << std::endl;
    } else {
        std::cerr << "[SnapLLM] Failed to reload model: " << name << " (after " << reload_ms << "ms)" << std::endl;
    }

    return success;
}

//=============================================================================
// MCB Integration Methods
//=============================================================================

bool ModelManager::is_loaded(const std::string& name) const {
    std::lock_guard<std::mutex> state_lock(state_mutex_);
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
    ModelInfo info;
    info.id = name;

    {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        if (loaded_models_.find(name) == loaded_models_.end()) {
            return std::nullopt;
        }
        auto path_it = model_paths_.find(name);
        if (path_it != model_paths_.end()) {
            info.path = path_it->second;
        }
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
