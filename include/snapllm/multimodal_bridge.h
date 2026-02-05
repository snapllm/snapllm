/**
 * @file multimodal_bridge.h
 * @brief Bridge for multimodal (vision/audio) support using llama.cpp's mtmd library
 *
 * This module provides integration with llama.cpp's mtmd library for vision-language
 * models like Qwen2.5-Omni, LLaVA, Gemma3, etc.
 */

#pragma once

#include "model_types.h"
#include <string>
#include <memory>
#include <vector>
#include <functional>

// Forward declarations
struct llama_model;
struct llama_context;
struct mtmd_context;
struct mtmd_bitmap;
struct mtmd_input_chunks;

namespace snapllm {

/**
 * @struct MultimodalConfig
 * @brief Configuration for multimodal model loading
 */
struct MultimodalConfig {
    std::string model_path;      // Path to main LLM model (.gguf)
    std::string mmproj_path;     // Path to multimodal projector (.gguf)
    bool use_gpu = true;         // Use GPU for vision encoding
    int n_threads = 4;           // Number of threads for encoding
    int n_gpu_layers = -1;       // GPU layers for LLM (-1 = all)
    int ctx_size = 4096;         // Context size
};

/**
 * @struct ImageInput
 * @brief Image input for multimodal inference
 */
struct ImageInput {
    std::string path;            // Path to image file
    std::vector<uint8_t> data;   // Raw RGB data (nx * ny * 3)
    uint32_t width = 0;
    uint32_t height = 0;
};

/**
 * @struct MultimodalResult
 * @brief Result from multimodal inference
 */
struct MultimodalResult {
    bool success = false;
    std::string response;
    std::string error_message;
    double encoding_time_ms = 0;
    double generation_time_ms = 0;
    int tokens_generated = 0;
    float tokens_per_second = 0;
};

/**
 * @struct MultimodalSamplingParams
 * @brief Sampling parameters for multimodal generation
 */
struct MultimodalSamplingParams {
    float temperature = 0.7f;
    float top_p = 0.9f;
    int top_k = 40;
    float repeat_penalty = 1.1f;
};

/**
 * @class MultimodalBridge
 * @brief Bridge for multimodal inference with vision-language models
 *
 * Supports models like:
 * - Qwen2.5-Omni (vision + audio)
 * - LLaVA (vision)
 * - Gemma3 (vision)
 * - MiniCPM-V (vision)
 */
class MultimodalBridge {
public:
    /**
     * @brief Token callback for streaming
     * @param token The generated token string
     * @return true to continue, false to stop
     */
    using TokenCallback = std::function<bool(const std::string& token)>;

    /**
     * @brief Construct multimodal bridge
     */
    MultimodalBridge();

    /**
     * @brief Destructor
     */
    ~MultimodalBridge();

    // Prevent copying
    MultimodalBridge(const MultimodalBridge&) = delete;
    MultimodalBridge& operator=(const MultimodalBridge&) = delete;

    /**
     * @brief Load a multimodal model with its projector
     *
     * @param config Model configuration
     * @return true if successful
     */
    bool load_model(const MultimodalConfig& config);

    /**
     * @brief Check if a model is loaded
     * @return true if model is loaded
     */
    bool is_loaded() const;

    /**
     * @brief Unload the current model
     */
    void unload();

    /**
     * @brief Check if model supports vision
     * @return true if vision is supported
     */
    bool supports_vision() const;

    /**
     * @brief Check if model supports audio
     * @return true if audio is supported
     */
    bool supports_audio() const;

    /**
     * @brief Generate response from text and image(s)
     *
     * The prompt should contain image markers where images should be inserted.
     * Default marker: "<__media__>" or use get_image_marker()
     *
     * Example:
     *   prompt = "What's in this image? <__media__>"
     *   images = [{path: "photo.jpg"}]
     *
     * @param prompt Text prompt with image markers
     * @param images Vector of image inputs (one per marker)
     * @param max_tokens Maximum tokens to generate
     * @param callback Optional token callback for streaming
     * @return Generation result
     */
    MultimodalResult generate(
        const std::string& prompt,
        const std::vector<ImageInput>& images,
        int max_tokens = 512,
        TokenCallback callback = nullptr
    );

    /**
     * @brief Generate response with sampling parameters
     */
    MultimodalResult generate(
        const std::string& prompt,
        const std::vector<ImageInput>& images,
        const MultimodalSamplingParams& sampling,
        int max_tokens = 512,
        TokenCallback callback = nullptr
    );

    /**
     * @brief Generate response from text only (no images)
     *
     * @param prompt Text prompt
     * @param max_tokens Maximum tokens to generate
     * @param callback Optional token callback for streaming
     * @return Generation result
     */
    MultimodalResult generate_text(
        const std::string& prompt,
        int max_tokens = 512,
        TokenCallback callback = nullptr
    );

    /**
     * @brief Get the image marker string for this model
     * @return Image marker (e.g., "<__media__>")
     */
    std::string get_image_marker() const;

    /**
     * @brief Get model info
     * @return Model architecture name
     */
    std::string get_model_info() const;

    /**
     * @brief Load image from file
     * @param path Image file path
     * @return ImageInput with loaded data
     */
    static ImageInput load_image(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace snapllm
