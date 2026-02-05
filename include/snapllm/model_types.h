/**
 * @file model_types.h
 * @brief Unified model type definitions for SnapLLM
 *
 * Supports multiple model architectures:
 * - Text LLMs (llama.cpp backend)
 * - Image Diffusion (stable-diffusion.cpp backend)
 * - Video Diffusion (future)
 * - Multimodal (vision-language models)
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <algorithm>
#include <cctype>

namespace snapllm {

/**
 * @enum ModelType
 * @brief Supported model architectures
 */
enum class ModelType {
    TEXT_LLM,           // Autoregressive text generation (llama.cpp)
    IMAGE_DIFFUSION,    // Stable Diffusion, SDXL, FLUX, etc.
    VIDEO_DIFFUSION,    // Wan2.1, CogVideoX, AnimateDiff
    MULTIMODAL_VL,      // Vision-Language (LLaVA, Qwen-VL)
    AUDIO_TTS,          // Text-to-Speech (future)
    AUDIO_STT,          // Speech-to-Text (future)
    UNKNOWN
};

/**
 * @enum DiffusionScheduler
 * @brief Sampling schedulers for diffusion models
 */
enum class DiffusionScheduler {
    EULER,              // Euler sampler
    EULER_A,            // Euler Ancestral
    HEUN,               // Heun's method
    DPM_PP_2M,          // DPM++ 2M
    DPM_PP_2M_KARRAS,   // DPM++ 2M Karras
    DPM_PP_SDE,         // DPM++ SDE
    LCM,                // Latent Consistency Model
    DDIM,               // DDIM
    DDPM,               // DDPM
    PNDM,               // PNDM
    DEFAULT
};

/**
 * @struct ImageSize
 * @brief Image dimensions
 */
struct ImageSize {
    int width = 512;
    int height = 512;

    size_t pixels() const { return static_cast<size_t>(width) * height; }
    size_t bytes_rgb() const { return pixels() * 3; }
    size_t bytes_rgba() const { return pixels() * 4; }
};

/**
 * @struct TextGenerationParams
 * @brief Parameters for text LLM generation
 */
struct TextGenerationParams {
    std::string prompt;
    int max_tokens = 512;
    float temperature = 0.7f;
    float top_p = 0.9f;
    int top_k = 40;
    float repeat_penalty = 1.1f;
    std::string stop_sequence;
    bool stream = false;
};

/**
 * @struct ImageGenerationParams
 * @brief Parameters for image generation
 */
struct ImageGenerationParams {
    std::string prompt;
    std::string negative_prompt;
    ImageSize size = {512, 512};
    int steps = 20;
    float cfg_scale = 7.0f;          // Classifier-free guidance scale
    int64_t seed = -1;               // -1 for random
    DiffusionScheduler scheduler = DiffusionScheduler::EULER_A;
    int batch_size = 1;
    float strength = 0.75f;          // For img2img
    std::string input_image_path;    // For img2img
    std::string controlnet_image;    // For ControlNet
    std::string lora_path;           // LoRA weights
    float lora_strength = 1.0f;
};

/**
 * @struct VideoGenerationParams
 * @brief Parameters for video generation
 */
struct VideoGenerationParams {
    std::string prompt;
    std::string negative_prompt;
    ImageSize frame_size = {512, 512};
    int num_frames = 24;
    int fps = 8;
    int steps = 25;
    float cfg_scale = 7.5f;
    int64_t seed = -1;
    DiffusionScheduler scheduler = DiffusionScheduler::EULER_A;
    std::string input_image_path;    // For image-to-video
};

/**
 * @struct GenerationResult
 * @brief Unified result from any generation
 */
struct GenerationResult {
    ModelType model_type;
    bool success = false;
    std::string error_message;

    // Text generation results
    std::string text;
    size_t tokens_generated = 0;
    double tokens_per_second = 0.0;

    // Image generation results
    std::vector<std::vector<uint8_t>> images;  // RGB data for each image
    ImageSize image_size;

    // Video generation results
    std::vector<std::vector<uint8_t>> frames;  // RGB data for each frame
    int fps = 0;

    // Timing
    double generation_time_ms = 0.0;
    double load_time_ms = 0.0;
};

/**
 * @struct ModelInfo
 * @brief Information about a loaded model
 */
struct UnifiedModelInfo {
    std::string name;
    std::string path;
    ModelType type = ModelType::UNKNOWN;
    std::string architecture;        // e.g., "llama", "sd15", "sdxl", "flux"
    std::string quantization;        // e.g., "Q4_K_M", "F16"
    size_t size_bytes = 0;
    size_t vram_usage_mb = 0;
    bool is_loaded = false;
    bool is_in_vram = false;
};

/**
 * @brief Detect model type from file path
 */
inline ModelType detect_model_type(const std::string& path) {
    // Convert to lowercase for comparison
    std::string lower_path = path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);

    // Check for diffusion model indicators
    if (lower_path.find("stable-diffusion") != std::string::npos ||
        lower_path.find("sd_") != std::string::npos ||
        lower_path.find("sd1") != std::string::npos ||
        lower_path.find("sd2") != std::string::npos ||
        lower_path.find("sdxl") != std::string::npos ||
        lower_path.find("flux") != std::string::npos ||
        lower_path.find("unet") != std::string::npos ||
        lower_path.find(".safetensors") != std::string::npos) {
        return ModelType::IMAGE_DIFFUSION;
    }

    // Check for video models
    if (lower_path.find("wan2") != std::string::npos ||
        lower_path.find("cogvideo") != std::string::npos ||
        lower_path.find("animatediff") != std::string::npos ||
        lower_path.find("ti2v") != std::string::npos ||
        lower_path.find("t2v") != std::string::npos) {
        return ModelType::VIDEO_DIFFUSION;
    }

    // Check for multimodal
    if (lower_path.find("llava") != std::string::npos ||
        lower_path.find("qwen-vl") != std::string::npos ||
        lower_path.find("moondream") != std::string::npos ||
        lower_path.find("bakllava") != std::string::npos) {
        return ModelType::MULTIMODAL_VL;
    }

    // Default to text LLM for .gguf files
    if (lower_path.find(".gguf") != std::string::npos) {
        return ModelType::TEXT_LLM;
    }

    return ModelType::UNKNOWN;
}

/**
 * @brief Get string representation of model type
 */
inline std::string model_type_to_string(ModelType type) {
    switch (type) {
        case ModelType::TEXT_LLM: return "Text LLM";
        case ModelType::IMAGE_DIFFUSION: return "Image Diffusion";
        case ModelType::VIDEO_DIFFUSION: return "Video Diffusion";
        case ModelType::MULTIMODAL_VL: return "Multimodal (Vision-Language)";
        case ModelType::AUDIO_TTS: return "Text-to-Speech";
        case ModelType::AUDIO_STT: return "Speech-to-Text";
        default: return "Unknown";
    }
}

/**
 * @brief Get string representation of scheduler
 */
inline std::string scheduler_to_string(DiffusionScheduler sched) {
    switch (sched) {
        case DiffusionScheduler::EULER: return "euler";
        case DiffusionScheduler::EULER_A: return "euler_a";
        case DiffusionScheduler::HEUN: return "heun";
        case DiffusionScheduler::DPM_PP_2M: return "dpm++2m";
        case DiffusionScheduler::DPM_PP_2M_KARRAS: return "dpm++2m_karras";
        case DiffusionScheduler::DPM_PP_SDE: return "dpm++sde";
        case DiffusionScheduler::LCM: return "lcm";
        case DiffusionScheduler::DDIM: return "ddim";
        case DiffusionScheduler::DDPM: return "ddpm";
        case DiffusionScheduler::PNDM: return "pndm";
        default: return "default";
    }
}

} // namespace snapllm
