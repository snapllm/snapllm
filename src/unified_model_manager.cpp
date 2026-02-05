/**
 * @file unified_model_manager.cpp
 * @brief Implementation of UnifiedModelManager for multi-modal inference
 */

#include "snapllm/unified_model_manager.h"
#include <iostream>
#include <chrono>
#include <filesystem>

namespace snapllm {

UnifiedModelManager::UnifiedModelManager(const std::string& workspace_root)
    : workspace_root_(workspace_root) {

    std::cout << "\n========================================" << std::endl;
    std::cout << "  SnapLLM Unified Model Manager" << std::endl;
    std::cout << "  Multi-Modal Inference Engine" << std::endl;
    std::cout << "========================================\n" << std::endl;

    std::cout << "[UnifiedManager] Workspace: " << workspace_root_ << std::endl;
    std::cout << "[UnifiedManager] Supported types:" << std::endl;
    std::cout << "  - Text LLMs (llama.cpp backend)" << std::endl;
    std::cout << "  - Image Diffusion (stable-diffusion.cpp backend)" << std::endl;
    std::cout << "  - Video Diffusion (experimental)" << std::endl;
    std::cout << "[UnifiedManager] VRAM Budget: " << VRAM_BUDGET_MB << " MB\n" << std::endl;

    // Create workspace directory
    std::filesystem::create_directories(workspace_root_);
}

UnifiedModelManager::~UnifiedModelManager() = default;

void UnifiedModelManager::ensure_llm_bridge() {
    if (!llm_bridge_) {
        std::cout << "[UnifiedManager] Initializing LLM bridge..." << std::endl;
        llm_bridge_ = std::make_unique<VPIDBridge>(workspace_root_);
    }
}

void UnifiedModelManager::ensure_diffusion_bridge() {
    if (!diffusion_bridge_) {
        std::cout << "[UnifiedManager] Initializing Diffusion bridge..." << std::endl;
        std::string diffusion_workspace = workspace_root_ + "/diffusion";
        diffusion_bridge_ = std::make_unique<DiffusionBridge>(diffusion_workspace);
    }
}

bool UnifiedModelManager::load_model(
    const std::string& model_name,
    const std::string& model_path,
    bool force_reload
) {
    // Auto-detect model type
    ModelType type = detect_model_type(model_path);

    if (type == ModelType::UNKNOWN) {
        std::cerr << "[UnifiedManager] Error: Could not detect model type for: " << model_path << std::endl;
        return false;
    }

    return load_model(model_name, model_path, type, force_reload);
}

bool UnifiedModelManager::load_model(
    const std::string& model_name,
    const std::string& model_path,
    ModelType type,
    bool force_reload
) {
    std::cout << "\n[UnifiedManager] Loading model: " << model_name << std::endl;
    std::cout << "[UnifiedManager] Path: " << model_path << std::endl;
    std::cout << "[UnifiedManager] Type: " << model_type_to_string(type) << std::endl;

    bool success = false;

    switch (type) {
        case ModelType::TEXT_LLM:
        case ModelType::MULTIMODAL_VL:
            ensure_llm_bridge();
            success = llm_bridge_->load_and_dequantize_model(model_name, model_path, force_reload);
            break;

        case ModelType::IMAGE_DIFFUSION:
            ensure_diffusion_bridge();
            success = diffusion_bridge_->load_model(model_name, model_path, "", force_reload);
            break;

        case ModelType::VIDEO_DIFFUSION:
            ensure_diffusion_bridge();
            // Video models use same diffusion bridge
            success = diffusion_bridge_->load_model(model_name, model_path, "", force_reload);
            break;

        default:
            std::cerr << "[UnifiedManager] Error: Unsupported model type" << std::endl;
            return false;
    }

    if (success) {
        // Register in model registry
        register_model(model_name, model_path, type);

        // Set as current if none set
        if (current_model_.empty()) {
            current_model_ = model_name;
        }
    }

    return success;
}

bool UnifiedModelManager::load_diffusion_model(
    const std::string& model_name,
    const std::string& unet_path,
    const std::string& clip_l_path,
    const std::string& clip_g_path,
    const std::string& t5xxl_path,
    const std::string& vae_path
) {
    ensure_diffusion_bridge();

    bool success = diffusion_bridge_->load_flux_model(
        model_name, unet_path, clip_l_path, t5xxl_path, vae_path
    );

    if (success) {
        register_model(model_name, unet_path, ModelType::IMAGE_DIFFUSION);
        if (current_model_.empty()) {
            current_model_ = model_name;
        }
    }

    return success;
}

GenerationResult UnifiedModelManager::generate_text(
    const std::string& model_name,
    const TextGenerationParams& params
) {
    GenerationResult result;
    result.model_type = ModelType::TEXT_LLM;

    if (!llm_bridge_ || !llm_bridge_->is_model_loaded(model_name)) {
        result.error_message = "Model not loaded: " + model_name;
        return result;
    }

    auto start = std::chrono::high_resolution_clock::now();

    size_t tokens_generated = 0;
    result.text = llm_bridge_->generate_text(model_name, params.prompt, params.max_tokens, &tokens_generated);

    auto end = std::chrono::high_resolution_clock::now();
    result.generation_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
    result.tokens_generated = tokens_generated;
    result.tokens_per_second = (tokens_generated > 0) ?
        (tokens_generated / result.generation_time_ms) * 1000.0 : 0.0;
    result.success = true;

    return result;
}

GenerationResult UnifiedModelManager::generate_image(
    const std::string& model_name,
    const ImageGenerationParams& params
) {
    if (!diffusion_bridge_ || !diffusion_bridge_->is_model_loaded(model_name)) {
        GenerationResult result;
        result.model_type = ModelType::IMAGE_DIFFUSION;
        result.error_message = "Model not loaded: " + model_name;
        return result;
    }

    return diffusion_bridge_->generate_image(model_name, params);
}

GenerationResult UnifiedModelManager::generate_video(
    const std::string& model_name,
    const VideoGenerationParams& params
) {
    GenerationResult result;
    result.model_type = ModelType::VIDEO_DIFFUSION;

    // TODO: Implement video generation using stable-diffusion.cpp's generate_video()
    result.error_message = "Video generation not yet implemented";
    return result;
}

GenerationResult UnifiedModelManager::generate(
    const std::string& model_name,
    const std::string& prompt,
    int max_tokens_or_steps
) {
    // Get model type from registry
    ModelType type = get_model_type(model_name);

    switch (type) {
        case ModelType::TEXT_LLM:
        case ModelType::MULTIMODAL_VL: {
            TextGenerationParams params;
            params.prompt = prompt;
            params.max_tokens = max_tokens_or_steps;
            return generate_text(model_name, params);
        }

        case ModelType::IMAGE_DIFFUSION: {
            ImageGenerationParams params;
            params.prompt = prompt;
            params.steps = max_tokens_or_steps;
            return generate_image(model_name, params);
        }

        case ModelType::VIDEO_DIFFUSION: {
            VideoGenerationParams params;
            params.prompt = prompt;
            params.steps = max_tokens_or_steps;
            return generate_video(model_name, params);
        }

        default: {
            GenerationResult result;
            result.error_message = "Unknown model type for: " + model_name;
            return result;
        }
    }
}

bool UnifiedModelManager::switch_model(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    auto it = model_registry_.find(model_name);
    if (it == model_registry_.end()) {
        std::cerr << "[UnifiedManager] Model not found: " << model_name << std::endl;
        return false;
    }

    current_model_ = model_name;
    std::cout << "[UnifiedManager] Switched to model: " << model_name << std::endl;
    return true;
}

std::string UnifiedModelManager::get_current_model() const {
    return current_model_;
}

void UnifiedModelManager::unload_model(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    auto it = model_registry_.find(model_name);
    if (it == model_registry_.end()) return;

    ModelType type = it->second.type;

    switch (type) {
        case ModelType::TEXT_LLM:
        case ModelType::MULTIMODAL_VL:
            if (llm_bridge_) llm_bridge_->unload_model(model_name);
            break;

        case ModelType::IMAGE_DIFFUSION:
        case ModelType::VIDEO_DIFFUSION:
            if (diffusion_bridge_) diffusion_bridge_->unload_model(model_name);
            break;

        default:
            break;
    }

    model_registry_.erase(it);

    if (current_model_ == model_name) {
        current_model_ = model_registry_.empty() ? "" : model_registry_.begin()->first;
    }
}

bool UnifiedModelManager::is_model_loaded(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    return model_registry_.count(model_name) > 0;
}

ModelType UnifiedModelManager::get_model_type(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = model_registry_.find(model_name);
    return it != model_registry_.end() ? it->second.type : ModelType::UNKNOWN;
}

std::vector<std::string> UnifiedModelManager::get_loaded_models() const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    std::vector<std::string> models;
    for (const auto& [name, entry] : model_registry_) {
        models.push_back(name);
    }
    return models;
}

std::vector<std::string> UnifiedModelManager::get_loaded_models(ModelType type) const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    std::vector<std::string> models;
    for (const auto& [name, entry] : model_registry_) {
        if (entry.type == type) {
            models.push_back(name);
        }
    }
    return models;
}

UnifiedModelInfo UnifiedModelManager::get_model_info(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    UnifiedModelInfo info;
    auto it = model_registry_.find(model_name);
    if (it == model_registry_.end()) return info;

    info.name = it->second.name;
    info.path = it->second.path;
    info.type = it->second.type;
    info.is_loaded = it->second.is_loaded;

    return info;
}

void UnifiedModelManager::enable_validation(bool enabled) {
    if (llm_bridge_) {
        llm_bridge_->enable_validation(enabled);
    }
}

void UnifiedModelManager::set_progress_callback(ProgressCallback callback) {
    progress_callback_ = callback;

    // Forward to bridges
    if (diffusion_bridge_) {
        diffusion_bridge_->set_progress_callback(
            [callback](int step, int total, double time_ms) {
                callback("diffusion", step, total, time_ms);
            }
        );
    }
}

void UnifiedModelManager::print_cache_stats() const {
    std::cout << "\n=== Unified Model Manager Stats ===" << std::endl;
    std::cout << "Total VRAM Budget: " << VRAM_BUDGET_MB << " MB" << std::endl;
    std::cout << "Current VRAM Usage: " << get_total_vram_usage_mb() << " MB" << std::endl;

    std::cout << "\nLoaded Models:" << std::endl;
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        for (const auto& [name, entry] : model_registry_) {
            std::cout << "  - " << name << " (" << model_type_to_string(entry.type) << ")" << std::endl;
        }
    }

    if (llm_bridge_) {
        std::cout << "\nLLM Bridge:" << std::endl;
        std::cout << "  VRAM Used: " << llm_bridge_->get_workspace(current_model_) << std::endl;
    }

    if (diffusion_bridge_) {
        std::cout << "\nDiffusion Bridge:" << std::endl;
        std::cout << "  VRAM Used: " << diffusion_bridge_->get_vram_used_mb() << " MB" << std::endl;
    }
}

size_t UnifiedModelManager::get_total_vram_usage_mb() const {
    size_t total = 0;

    // Note: This is approximate as both bridges share the same GPU
    if (diffusion_bridge_) {
        total += diffusion_bridge_->get_vram_used_mb();
    }

    // LLM bridge VRAM tracking would need to be added
    // For now, return diffusion bridge usage

    return total;
}

bool UnifiedModelManager::save_image(
    const GenerationResult& result,
    const std::string& output_path,
    int image_index
) {
    if (result.images.empty() || image_index >= static_cast<int>(result.images.size())) {
        return false;
    }

    return DiffusionBridge::save_image(result.images[image_index], result.image_size, output_path);
}

bool UnifiedModelManager::save_video(
    const GenerationResult& result,
    const std::string& output_path
) {
    // TODO: Implement video saving (requires video encoding library)
    return false;
}

void UnifiedModelManager::register_model(const std::string& name, const std::string& path, ModelType type) {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    ModelEntry entry;
    entry.name = name;
    entry.path = path;
    entry.type = type;
    entry.is_loaded = true;

    model_registry_[name] = entry;
}

} // namespace snapllm
