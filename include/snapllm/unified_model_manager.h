/**
 * @file unified_model_manager.h
 * @brief Unified model manager for all model types
 *
 * Provides a single interface for managing:
 * - Text LLMs (via VPIDBridge)
 * - Image Diffusion (via DiffusionBridge)
 * - Video Diffusion (future)
 * - Multimodal models (future)
 *
 * Key features:
 * - Automatic model type detection
 * - Unified VRAM management across all model types
 * - vPID caching for fast model switching
 * - Single API for all inference types
 */

#pragma once

#include "model_types.h"
#include "vpid_bridge.h"
#include "diffusion_bridge.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <functional>

namespace snapllm {

/**
 * @class UnifiedModelManager
 * @brief Single manager for all model types
 *
 * Usage:
 * @code
 * UnifiedModelManager manager("D:\\SnapLLM_Workspace");
 *
 * // Load text LLM
 * manager.load_model("medicine", "D:\\Models\\medicine-llm.Q8_0.gguf");
 *
 * // Load image model
 * manager.load_model("sdxl", "D:\\Models\\sdxl-base.safetensors");
 *
 * // Generate text
 * TextGenerationParams text_params;
 * text_params.prompt = "What is diabetes?";
 * auto result = manager.generate("medicine", text_params);
 *
 * // Generate image
 * ImageGenerationParams img_params;
 * img_params.prompt = "A beautiful sunset over mountains";
 * auto result = manager.generate("sdxl", img_params);
 * @endcode
 */
class UnifiedModelManager {
public:
    /**
     * @brief Progress callback for generation
     */
    using ProgressCallback = std::function<void(const std::string& stage, int current, int total, double time_ms)>;

    /**
     * @brief Construct unified manager
     * @param workspace_root Root directory for all workspaces
     */
    explicit UnifiedModelManager(const std::string& workspace_root = "D:\\SnapLLM_Workspace");

    /**
     * @brief Destructor
     */
    ~UnifiedModelManager();

    // Prevent copying
    UnifiedModelManager(const UnifiedModelManager&) = delete;
    UnifiedModelManager& operator=(const UnifiedModelManager&) = delete;

    /**
     * @brief Load any model (auto-detects type)
     *
     * @param model_name Name to identify this model
     * @param model_path Path to model file
     * @param force_reload Force reload even if cached
     * @return true if successful
     */
    bool load_model(
        const std::string& model_name,
        const std::string& model_path,
        bool force_reload = false
    );

    /**
     * @brief Load model with explicit type
     *
     * @param model_name Name to identify this model
     * @param model_path Path to model file
     * @param type Model type
     * @param force_reload Force reload even if cached
     * @return true if successful
     */
    bool load_model(
        const std::string& model_name,
        const std::string& model_path,
        ModelType type,
        bool force_reload = false
    );

    /**
     * @brief Load diffusion model with separate components
     *
     * For SDXL/FLUX models with separate encoder files.
     *
     * @param model_name Name to identify this model
     * @param unet_path Path to UNet
     * @param clip_l_path Path to CLIP-L
     * @param clip_g_path Path to CLIP-G (SDXL) or empty
     * @param t5xxl_path Path to T5-XXL (FLUX/SD3) or empty
     * @param vae_path Path to VAE
     * @return true if successful
     */
    bool load_diffusion_model(
        const std::string& model_name,
        const std::string& unet_path,
        const std::string& clip_l_path,
        const std::string& clip_g_path = "",
        const std::string& t5xxl_path = "",
        const std::string& vae_path = ""
    );

    /**
     * @brief Generate text from prompt
     *
     * @param model_name LLM model to use
     * @param params Text generation parameters
     * @return Generation result
     */
    GenerationResult generate_text(
        const std::string& model_name,
        const TextGenerationParams& params
    );

    /**
     * @brief Generate image from prompt
     *
     * @param model_name Diffusion model to use
     * @param params Image generation parameters
     * @return Generation result with image data
     */
    GenerationResult generate_image(
        const std::string& model_name,
        const ImageGenerationParams& params
    );

    /**
     * @brief Generate video from prompt
     *
     * @param model_name Video model to use
     * @param params Video generation parameters
     * @return Generation result with frame data
     */
    GenerationResult generate_video(
        const std::string& model_name,
        const VideoGenerationParams& params
    );

    /**
     * @brief Convenience method - auto-detects generation type
     *
     * @param model_name Model to use
     * @param prompt Text prompt
     * @param max_tokens_or_steps Max tokens (LLM) or steps (diffusion)
     * @return Generation result
     */
    GenerationResult generate(
        const std::string& model_name,
        const std::string& prompt,
        int max_tokens_or_steps = 50
    );

    /**
     * @brief Switch active model (for session-based interaction)
     * @param model_name Model to switch to
     * @return true if successful
     */
    bool switch_model(const std::string& model_name);

    /**
     * @brief Get current active model name
     */
    std::string get_current_model() const;

    /**
     * @brief Unload model from memory
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
     * @brief Get type of loaded model
     * @param model_name Model name
     * @return Model type or UNKNOWN if not loaded
     */
    ModelType get_model_type(const std::string& model_name) const;

    /**
     * @brief Get list of all loaded models
     * @return Vector of model names
     */
    std::vector<std::string> get_loaded_models() const;

    /**
     * @brief Get list of loaded models by type
     * @param type Model type filter
     * @return Vector of model names
     */
    std::vector<std::string> get_loaded_models(ModelType type) const;

    /**
     * @brief Get model info
     * @param model_name Model name
     * @return Model info
     */
    UnifiedModelInfo get_model_info(const std::string& model_name) const;

    /**
     * @brief Enable/disable validation
     */
    void enable_validation(bool enabled);

    /**
     * @brief Set progress callback
     */
    void set_progress_callback(ProgressCallback callback);

    /**
     * @brief Print cache statistics
     */
    void print_cache_stats() const;

    /**
     * @brief Get total VRAM usage across all models
     */
    size_t get_total_vram_usage_mb() const;

    /**
     * @brief Get VRAM budget
     */
    size_t get_vram_budget_mb() const { return VRAM_BUDGET_MB; }

    /**
     * @brief Save generated image to file
     */
    static bool save_image(
        const GenerationResult& result,
        const std::string& output_path,
        int image_index = 0
    );

    /**
     * @brief Save generated video to file
     */
    static bool save_video(
        const GenerationResult& result,
        const std::string& output_path
    );

private:
    std::string workspace_root_;
    std::string current_model_;
    ProgressCallback progress_callback_;

    // Bridges for different model types
    std::unique_ptr<VPIDBridge> llm_bridge_;
    std::unique_ptr<DiffusionBridge> diffusion_bridge_;

    // Model registry
    struct ModelEntry {
        std::string name;
        std::string path;
        ModelType type;
        bool is_loaded;
    };
    std::unordered_map<std::string, ModelEntry> model_registry_;
    mutable std::mutex registry_mutex_;

    // Unified VRAM management
    static constexpr size_t VRAM_BUDGET_MB = 7000;

    /**
     * @brief Initialize bridges lazily
     */
    void ensure_llm_bridge();
    void ensure_diffusion_bridge();

    /**
     * @brief Register model in registry
     */
    void register_model(const std::string& name, const std::string& path, ModelType type);
};

} // namespace snapllm
