/**
 * @file diffusion_bridge.h
 * @brief Bridge between vPID cache and stable-diffusion.cpp
 *
 * This module provides integration with stable-diffusion.cpp for image generation.
 * Similar to VPIDBridge for LLMs, it enables:
 * - Fast model loading with vPID caching
 * - Multiple SD model management
 * - Efficient VRAM utilization
 */

#pragma once

#include "model_types.h"
#include "vpid_workspace.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <functional>

// Forward declaration for stable-diffusion.cpp context
// sd_image_t is a typedef, so we can't forward declare it
// It's only used internally in the .cpp implementation
struct sd_ctx_t;

namespace snapllm {

/**
 * @struct DiffusionModelInfo
 * @brief Information about a loaded diffusion model
 */
struct DiffusionModelInfo {
    std::string name;
    std::string model_path;           // Single-file model path (SD1.5, SDXL single)
    std::string diffusion_model_path; // Separate diffusion model (Wan2, SD3, FLUX)
    std::string vae_path;
    std::string clip_l_path;
    std::string clip_g_path;
    std::string clip_vision_path;     // For Wan2 I2V
    std::string t5xxl_path;           // T5-XXL or UMT5-XXL encoder
    std::string lora_path;
    std::string controlnet_path;
    std::string high_noise_model_path; // For Wan2.2 dual-model

    std::string architecture;    // "sd15", "sd21", "sdxl", "sd3", "flux", "wan21", "wan22"
    ImageSize default_size;
    size_t vram_usage_mb = 0;
    bool is_loaded = false;
    bool is_video_model = false;
};

/**
 * @struct MultiFileModelParams
 * @brief Parameters for loading multi-file models (SD3, FLUX, Wan2)
 */
struct MultiFileModelParams {
    std::string model_name;
    std::string diffusion_model_path;  // Main diffusion/UNet model
    std::string vae_path;              // VAE for encoding/decoding
    std::string t5xxl_path;            // T5-XXL or UMT5-XXL text encoder
    std::string clip_l_path;           // CLIP-L text encoder (SD3)
    std::string clip_g_path;           // CLIP-G text encoder (SD3/SDXL)
    std::string clip_vision_path;      // CLIP vision encoder (Wan2 I2V)
    std::string high_noise_model_path; // High noise model (Wan2.2)
    bool offload_to_cpu = true;        // Offload params to CPU when not in use
};

/**
 * @class DiffusionBridge
 * @brief Bridge between vPID cache and stable-diffusion.cpp
 *
 * Provides image generation capabilities with vPID caching for fast model switching.
 */
class DiffusionBridge {
public:
    /**
     * @brief Progress callback type
     * @param step Current step
     * @param total_steps Total steps
     * @param time_ms Time elapsed in ms
     */
    using ProgressCallback = std::function<void(int step, int total_steps, double time_ms)>;

    /**
     * @brief Construct bridge with workspace root
     * @param workspace_root Root directory for diffusion model workspaces
     */
    explicit DiffusionBridge(const std::string& workspace_root = "D:\\SnapLLM_Workspace\\diffusion");

    /**
     * @brief Destructor
     */
    ~DiffusionBridge();

    // Prevent copying
    DiffusionBridge(const DiffusionBridge&) = delete;
    DiffusionBridge& operator=(const DiffusionBridge&) = delete;

    /**
     * @brief Load a Stable Diffusion model
     *
     * Supports multiple model formats:
     * - .safetensors (SD 1.5, SDXL, SD3, FLUX)
     * - .ckpt (legacy checkpoint format)
     * - GGUF quantized models (stable-diffusion.cpp format)
     *
     * @param model_name Name to identify this model
     * @param model_path Path to model file
     * @param vae_path Optional path to VAE (uses built-in if empty)
     * @param force_reload Force reload even if cached
     * @return true if successful
     */
    bool load_model(
        const std::string& model_name,
        const std::string& model_path,
        const std::string& vae_path = "",
        bool force_reload = false
    );

    /**
     * @brief Load SDXL or FLUX model with separate components
     *
     * @param model_name Name to identify this model
     * @param unet_path Path to UNet/transformer model
     * @param clip_l_path Path to CLIP-L text encoder
     * @param clip_g_path Path to CLIP-G text encoder (SDXL)
     * @param t5xxl_path Path to T5-XXL encoder (FLUX/SD3)
     * @param vae_path Path to VAE
     * @return true if successful
     */
    bool load_flux_model(
        const std::string& model_name,
        const std::string& unet_path,
        const std::string& clip_l_path,
        const std::string& t5xxl_path,
        const std::string& vae_path
    );

    /**
     * @brief Load multi-file model (SD3, FLUX, Wan2)
     *
     * Used for models that require separate component files:
     * - SD3: diffusion model + clip_l + clip_g + t5xxl + vae
     * - FLUX: diffusion model + clip_l + t5xxl + vae
     * - Wan2: diffusion model + umt5xxl + vae (+ clip_vision for I2V)
     *
     * @param params Model loading parameters
     * @return true if successful
     */
    bool load_multifile_model(const MultiFileModelParams& params);

    /**
     * @brief Generate image from text prompt
     *
     * @param model_name Model to use
     * @param params Generation parameters
     * @return Generation result with image data
     */
    GenerationResult generate_image(
        const std::string& model_name,
        const ImageGenerationParams& params
    );

    /**
     * @brief Generate image from another image (img2img)
     *
     * @param model_name Model to use
     * @param input_image Input image path
     * @param params Generation parameters
     * @return Generation result with image data
     */
    GenerationResult img2img(
        const std::string& model_name,
        const std::string& input_image,
        const ImageGenerationParams& params
    );

    /**
     * @brief Generate video from text prompt
     *
     * For video models like Wan2, CogVideoX, etc.
     *
     * @param model_name Model to use
     * @param params Video generation parameters
     * @return Generation result with frame data
     */
    GenerationResult generate_video(
        const std::string& model_name,
        const VideoGenerationParams& params
    );

    /**
     * @brief Apply LoRA weights to model
     *
     * @param model_name Model to apply LoRA to
     * @param lora_path Path to LoRA file
     * @param strength LoRA strength (0.0 - 1.0)
     * @return true if successful
     */
    bool apply_lora(
        const std::string& model_name,
        const std::string& lora_path,
        float strength = 1.0f
    );

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
     * @brief Get list of loaded models
     * @return Vector of model names
     */
    std::vector<std::string> get_loaded_models() const;

    /**
     * @brief Get model info
     * @param model_name Model name
     * @return Model info or nullptr if not loaded
     */
    const DiffusionModelInfo* get_model_info(const std::string& model_name) const;

    /**
     * @brief Set progress callback
     * @param callback Progress callback function
     */
    void set_progress_callback(ProgressCallback callback);

    /**
     * @brief Save image to file
     * @param image_data RGB image data
     * @param size Image dimensions
     * @param output_path Output file path
     * @return true if successful
     */
    static bool save_image(
        const std::vector<uint8_t>& image_data,
        const ImageSize& size,
        const std::string& output_path
    );

    /**
     * @brief Get VRAM budget in MB
     */
    size_t get_vram_budget_mb() const { return VRAM_BUDGET_MB; }

    /**
     * @brief Get current VRAM usage in MB
     */
    size_t get_vram_used_mb() const { return total_vram_used_; }

private:
    std::string workspace_root_;
    ProgressCallback progress_callback_;

    // Model storage
    std::unordered_map<std::string, DiffusionModelInfo> model_info_;
    std::unordered_map<std::string, sd_ctx_t*> model_contexts_;
    mutable std::mutex models_mutex_;

    // VRAM management
    static constexpr size_t VRAM_BUDGET_MB = 12000;  // Increased for SD3.5 testing
    std::unordered_map<std::string, size_t> model_vram_usage_;
    size_t total_vram_used_ = 0;

    // vPID workspace for caching
    std::unordered_map<std::string, std::shared_ptr<VPIDWorkspace>> model_workspaces_;

    /**
     * @brief Ensure enough VRAM space by evicting models
     */
    bool ensure_vram_space(size_t needed_mb);

    /**
     * @brief Evict least recently used model
     */
    std::string evict_lru_model();

    /**
     * @brief Detect SD architecture from model file
     */
    std::string detect_architecture(const std::string& model_path);

    /**
     * @brief Get default image size for architecture
     */
    ImageSize get_default_size(const std::string& architecture);

    /**
     * @brief Convert scheduler enum to sd.cpp enum
     */
    int scheduler_to_sd_enum(DiffusionScheduler scheduler);
};

} // namespace snapllm
