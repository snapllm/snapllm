/**
 * @file diffusion_bridge.cpp
 * @brief Implementation of DiffusionBridge for stable-diffusion.cpp integration
 */

#include "snapllm/diffusion_bridge.h"
#include "stable-diffusion.h"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <map>

// CUDA synchronization for consecutive generations
#ifdef __CUDACC__
#include <cuda_runtime.h>
#else
// Forward declare for non-CUDA compilation
extern "C" {
    int cudaDeviceSynchronize();
}
#endif

// STB image write for saving output
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace snapllm {

// Global progress callback storage
static DiffusionBridge::ProgressCallback g_progress_callback;
static std::chrono::steady_clock::time_point g_gen_start;

// Progress callback bridge to C API
static void sd_progress_bridge(int step, int steps, float time, void* data) {
    if (g_progress_callback) {
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - g_gen_start).count();
        g_progress_callback(step, steps, elapsed);
    }
}

// Log callback for debugging - write to file since stdout/stderr capture issues on Windows threads
static void sd_log_bridge(sd_log_level_t level, const char* text, void* data) {
    const char* level_str = "INFO";
    switch (level) {
        case SD_LOG_DEBUG: level_str = "DEBUG"; break;
        case SD_LOG_INFO:  level_str = "INFO";  break;
        case SD_LOG_WARN:  level_str = "WARN";  break;
        case SD_LOG_ERROR: level_str = "ERROR"; break;
    }
    // Write to file for reliable capture
    FILE* f = fopen("D:/SnapLLM_Workspace/sd_debug.log", "a");
    if (f) {
        fprintf(f, "[SD %s] %s\n", level_str, text);
        fclose(f);
    }
}

DiffusionBridge::DiffusionBridge(const std::string& workspace_root)
    : workspace_root_(workspace_root) {

    // VERY FIRST OUTPUT - write to file for debugging (use C:\ temp as fallback)
    FILE* dbg = fopen("C:/temp/diffusion_init.log", "w");
    if (!dbg) dbg = fopen("D:/SnapLLM_Workspace/diffusion_init.log", "w");
    if (dbg) {
        fprintf(dbg, "DiffusionBridge constructor called\n");
        fprintf(dbg, "Workspace: %s\n", workspace_root.c_str());
        fclose(dbg);
    }

    // Create workspace directory
    std::filesystem::create_directories(workspace_root_);

    // Set up callbacks
    sd_set_log_callback(sd_log_bridge, nullptr);

    std::cout << "[DiffusionBridge] Initialized" << std::endl;
    std::cout << "[DiffusionBridge] Workspace: " << workspace_root_ << std::endl;
    std::cout << "[DiffusionBridge] System info: " << sd_get_system_info() << std::endl;
}

DiffusionBridge::~DiffusionBridge() {
    // Free all loaded contexts
    std::lock_guard<std::mutex> lock(models_mutex_);
    for (auto& [name, ctx] : model_contexts_) {
        if (ctx) {
            free_sd_ctx(ctx);
        }
    }
    model_contexts_.clear();
}

bool DiffusionBridge::load_model(
    const std::string& model_name,
    const std::string& model_path,
    const std::string& vae_path,
    bool force_reload
) {
    std::lock_guard<std::mutex> lock(models_mutex_);

    // Check if already loaded
    if (!force_reload && model_contexts_.count(model_name) > 0) {
        std::cout << "[DiffusionBridge] Model '" << model_name << "' already loaded" << std::endl;
        return true;
    }

    // Unload if reloading
    if (model_contexts_.count(model_name) > 0) {
        free_sd_ctx(model_contexts_[model_name]);
        model_contexts_.erase(model_name);
        if (model_vram_usage_.count(model_name)) {
            total_vram_used_ -= model_vram_usage_[model_name];
            model_vram_usage_.erase(model_name);
        }
    }

    std::cout << "[DiffusionBridge] Loading model: " << model_name << std::endl;
    std::cout << "[DiffusionBridge] Path: " << model_path << std::endl;

    // Detect architecture
    std::string arch = detect_architecture(model_path);
    std::cout << "[DiffusionBridge] Detected architecture: " << arch << std::endl;

    // Estimate VRAM usage (rough estimate based on file size)
    size_t file_size_mb = 0;
    try {
        file_size_mb = std::filesystem::file_size(model_path) / (1024 * 1024);
    } catch (...) {
        std::cerr << "[DiffusionBridge] Warning: Could not get file size" << std::endl;
    }

    // Diffusion models typically need ~2x file size in VRAM due to working memory
    size_t estimated_vram_mb = file_size_mb * 2;

    // Ensure VRAM space
    if (!ensure_vram_space(estimated_vram_mb)) {
        std::cerr << "[DiffusionBridge] Error: Not enough VRAM for model" << std::endl;
        return false;
    }

    // Initialize context parameters
    sd_ctx_params_t params;
    sd_ctx_params_init(&params);

    params.model_path = model_path.c_str();
    params.vae_path = vae_path.empty() ? nullptr : vae_path.c_str();
    params.n_threads = get_num_physical_cores();
    params.wtype = SD_TYPE_COUNT;  // Auto-detect
    params.rng_type = CUDA_RNG;
    params.diffusion_flash_attn = true;  // Enable flash attention if available

    // Create context
    sd_ctx_t* ctx = new_sd_ctx(&params);

    if (!ctx) {
        std::cerr << "[DiffusionBridge] Error: Failed to create context for " << model_name << std::endl;
        return false;
    }

    // Store context and info
    model_contexts_[model_name] = ctx;

    DiffusionModelInfo info;
    info.name = model_name;
    info.model_path = model_path;
    info.vae_path = vae_path;
    info.architecture = arch;
    info.default_size = get_default_size(arch);
    info.vram_usage_mb = estimated_vram_mb;
    info.is_loaded = true;
    model_info_[model_name] = info;

    // Update VRAM tracking
    model_vram_usage_[model_name] = estimated_vram_mb;
    total_vram_used_ += estimated_vram_mb;

    std::cout << "[DiffusionBridge] Model '" << model_name << "' loaded successfully!" << std::endl;
    std::cout << "[DiffusionBridge] VRAM usage: " << estimated_vram_mb << " MB" << std::endl;
    std::cout << "[DiffusionBridge] Total VRAM: " << total_vram_used_ << "/" << VRAM_BUDGET_MB << " MB" << std::endl;

    return true;
}

bool DiffusionBridge::load_flux_model(
    const std::string& model_name,
    const std::string& unet_path,
    const std::string& clip_l_path,
    const std::string& t5xxl_path,
    const std::string& vae_path
) {
    std::lock_guard<std::mutex> lock(models_mutex_);

    std::cout << "[DiffusionBridge] Loading FLUX model: " << model_name << std::endl;

    // Initialize context parameters for FLUX
    sd_ctx_params_t params;
    sd_ctx_params_init(&params);

    params.diffusion_model_path = unet_path.c_str();
    params.clip_l_path = clip_l_path.c_str();
    params.t5xxl_path = t5xxl_path.c_str();
    params.vae_path = vae_path.c_str();
    params.n_threads = get_num_physical_cores();
    params.prediction = FLUX_FLOW_PRED;
    params.diffusion_flash_attn = true;

    sd_ctx_t* ctx = new_sd_ctx(&params);

    if (!ctx) {
        std::cerr << "[DiffusionBridge] Error: Failed to create FLUX context" << std::endl;
        return false;
    }

    model_contexts_[model_name] = ctx;

    DiffusionModelInfo info;
    info.name = model_name;
    info.model_path = unet_path;
    info.clip_l_path = clip_l_path;
    info.t5xxl_path = t5xxl_path;
    info.vae_path = vae_path;
    info.architecture = "flux";
    info.default_size = {1024, 1024};
    info.is_loaded = true;
    model_info_[model_name] = info;

    return true;
}

bool DiffusionBridge::load_multifile_model(const MultiFileModelParams& params) {
    std::lock_guard<std::mutex> lock(models_mutex_);

    // Immediate debug output
    std::cout << "[DiffusionBridge] >>> load_multifile_model ENTRY <<<" << std::endl;
    std::cout.flush();

    std::cout << "[DiffusionBridge] Loading multi-file model: " << params.model_name << std::endl;
    std::cout << "[DiffusionBridge] Diffusion model: " << params.diffusion_model_path << std::endl;
    std::cout << "[DiffusionBridge] VAE: " << params.vae_path << std::endl;
    std::cout << "[DiffusionBridge] T5XXL: " << params.t5xxl_path << std::endl;

    // Detect architecture from diffusion model path
    std::string arch = detect_architecture(params.diffusion_model_path);
    std::cout << "[DiffusionBridge] Detected architecture: " << arch << std::endl;

    bool is_video = (arch == "wan21" || arch == "wan22");
    bool is_sd3 = (arch == "sd3" || arch == "sd35");

    // Validate required files
    if (params.diffusion_model_path.empty()) {
        std::cerr << "[DiffusionBridge] Error: diffusion_model_path is required" << std::endl;
        return false;
    }
    if (params.vae_path.empty()) {
        std::cerr << "[DiffusionBridge] Error: vae_path is required for " << arch << std::endl;
        return false;
    }
    if (params.t5xxl_path.empty()) {
        std::cerr << "[DiffusionBridge] Error: t5xxl_path is required for " << arch << std::endl;
        return false;
    }

    // Estimate VRAM usage
    size_t estimated_vram_mb = 0;
    try {
        estimated_vram_mb += std::filesystem::file_size(params.diffusion_model_path) / (1024 * 1024);
        estimated_vram_mb += std::filesystem::file_size(params.vae_path) / (1024 * 1024);
        estimated_vram_mb += std::filesystem::file_size(params.t5xxl_path) / (1024 * 1024);
        if (!params.clip_l_path.empty() && std::filesystem::exists(params.clip_l_path))
            estimated_vram_mb += std::filesystem::file_size(params.clip_l_path) / (1024 * 1024);
        if (!params.clip_g_path.empty() && std::filesystem::exists(params.clip_g_path))
            estimated_vram_mb += std::filesystem::file_size(params.clip_g_path) / (1024 * 1024);
        // Add working memory overhead
        estimated_vram_mb = (size_t)(estimated_vram_mb * 1.5);
    } catch (...) {
        estimated_vram_mb = 8000; // Default estimate
    }

    std::cout << "[DiffusionBridge] Estimated VRAM: " << estimated_vram_mb << " MB" << std::endl;

    // Initialize context parameters
    sd_ctx_params_t ctx_params;
    sd_ctx_params_init(&ctx_params);

    ctx_params.diffusion_model_path = params.diffusion_model_path.c_str();
    ctx_params.vae_path = params.vae_path.c_str();
    ctx_params.t5xxl_path = params.t5xxl_path.c_str();
    ctx_params.n_threads = get_num_physical_cores();
    ctx_params.diffusion_flash_attn = true;

    // VRAM optimization: For large models (Wan2, FLUX, SD3), keep T5/CLIP and VAE on CPU
    // to reduce VRAM usage. Only diffusion model stays on GPU (~3-4GB instead of 11-12GB)
    bool needs_vram_optimization = (estimated_vram_mb > 8000) || is_video;

    // Print to stdout for visibility
    std::cout << "[DiffusionBridge] VRAM check: estimated=" << estimated_vram_mb
              << ", is_video=" << is_video << ", needs_opt=" << needs_vram_optimization << std::endl;

    if (needs_vram_optimization) {
        std::cout << "[DiffusionBridge] ENABLING VRAM OPTIMIZATION: keep_clip_on_cpu=true, keep_vae_on_cpu=true" << std::endl;
        ctx_params.keep_clip_on_cpu = true;  // Keeps T5/CLIP encoder on CPU (~7GB savings)
        ctx_params.keep_vae_on_cpu = true;   // Keeps VAE on CPU (~1GB savings)
        // Note: offload_params_to_cpu causes tensor layout issues, so we don't use it
        ctx_params.offload_params_to_cpu = false;
    } else {
        ctx_params.offload_params_to_cpu = params.offload_to_cpu;
    }
    std::cout.flush();

    // Set optional paths
    if (!params.clip_l_path.empty())
        ctx_params.clip_l_path = params.clip_l_path.c_str();
    if (!params.clip_g_path.empty())
        ctx_params.clip_g_path = params.clip_g_path.c_str();
    if (!params.clip_vision_path.empty())
        ctx_params.clip_vision_path = params.clip_vision_path.c_str();
    if (!params.high_noise_model_path.empty())
        ctx_params.high_noise_diffusion_model_path = params.high_noise_model_path.c_str();

    // Set prediction type based on architecture
    if (is_sd3) {
        ctx_params.prediction = SD3_FLOW_PRED;
    } else if (arch == "flux") {
        ctx_params.prediction = FLUX_FLOW_PRED;
    }
    // Wan2 uses flow-based diffusion with flow_shift
    if (is_video) {
        ctx_params.flow_shift = 3.0f;  // Default for Wan2 video models
    }

    std::cout << "[DiffusionBridge] Creating context..." << std::endl;
    sd_ctx_t* ctx = new_sd_ctx(&ctx_params);

    if (!ctx) {
        std::cerr << "[DiffusionBridge] Error: Failed to create context for " << params.model_name << std::endl;
        return false;
    }

    // Store context and info
    model_contexts_[params.model_name] = ctx;

    DiffusionModelInfo info;
    info.name = params.model_name;
    info.diffusion_model_path = params.diffusion_model_path;
    info.vae_path = params.vae_path;
    info.t5xxl_path = params.t5xxl_path;
    info.clip_l_path = params.clip_l_path;
    info.clip_g_path = params.clip_g_path;
    info.clip_vision_path = params.clip_vision_path;
    info.high_noise_model_path = params.high_noise_model_path;
    info.architecture = arch;
    info.default_size = get_default_size(arch);
    info.vram_usage_mb = estimated_vram_mb;
    info.is_loaded = true;
    info.is_video_model = is_video;
    model_info_[params.model_name] = info;

    // Update VRAM tracking
    model_vram_usage_[params.model_name] = estimated_vram_mb;
    total_vram_used_ += estimated_vram_mb;

    std::cout << "[DiffusionBridge] Model '" << params.model_name << "' loaded successfully!" << std::endl;
    std::cout << "[DiffusionBridge] Architecture: " << arch << (is_video ? " (video)" : " (image)") << std::endl;
    std::cout << "[DiffusionBridge] VRAM usage: " << estimated_vram_mb << "/" << VRAM_BUDGET_MB << " MB" << std::endl;

    return true;
}

GenerationResult DiffusionBridge::generate_image(
    const std::string& model_name,
    const ImageGenerationParams& params
) {
    GenerationResult result;
    result.model_type = ModelType::IMAGE_DIFFUSION;

    std::lock_guard<std::mutex> lock(models_mutex_);

    // Check if model is loaded
    auto ctx_it = model_contexts_.find(model_name);
    if (ctx_it == model_contexts_.end() || !ctx_it->second) {
        result.error_message = "Model not loaded: " + model_name;
        return result;
    }

    sd_ctx_t* ctx = ctx_it->second;

    // Workaround for stable-diffusion.cpp CUDA buffer bug:
    // Recreate context if this is not the first generation
    // This ensures clean CUDA state between generations
    static std::map<std::string, int> generation_count;
    generation_count[model_name]++;
    int gen_num = generation_count[model_name];

    std::cout << "[DiffusionBridge] Generation #" << gen_num << " for model " << model_name << std::endl;
    std::cout.flush();

    if (gen_num > 1) {
        std::cout << "[DiffusionBridge] Recreating context for clean CUDA state..." << std::endl;
        std::cout.flush();

        // Get model info before freeing
        auto info_it = model_info_.find(model_name);
        if (info_it != model_info_.end()) {
            std::string model_path = info_it->second.model_path;
            std::string vae_path = info_it->second.vae_path;

            // Free old context
            free_sd_ctx(ctx);

            // Recreate context
            sd_ctx_params_t ctx_params;
            sd_ctx_params_init(&ctx_params);
            ctx_params.model_path = model_path.c_str();
            ctx_params.vae_path = vae_path.empty() ? nullptr : vae_path.c_str();
            ctx_params.n_threads = get_num_physical_cores();
            ctx_params.wtype = SD_TYPE_COUNT;
            ctx_params.rng_type = CUDA_RNG;
            ctx_params.diffusion_flash_attn = true;

            ctx = new_sd_ctx(&ctx_params);
            if (!ctx) {
                result.error_message = "Failed to recreate context";
                return result;
            }

            model_contexts_[model_name] = ctx;
            ctx_it = model_contexts_.find(model_name);
        }
    }

    std::cout << "\n[DiffusionBridge] Generating image..." << std::endl;
    std::cout << "[DiffusionBridge] Prompt: " << params.prompt << std::endl;
    std::cout << "[DiffusionBridge] Size: " << params.size.width << "x" << params.size.height << std::endl;
    std::cout << "[DiffusionBridge] Steps: " << params.steps << std::endl;

    // Set up progress callback
    g_progress_callback = progress_callback_;
    g_gen_start = std::chrono::steady_clock::now();
    sd_set_progress_callback(sd_progress_bridge, nullptr);

    // Initialize generation parameters
    sd_img_gen_params_t gen_params;
    sd_img_gen_params_init(&gen_params);

    gen_params.prompt = params.prompt.c_str();
    gen_params.negative_prompt = params.negative_prompt.empty() ? nullptr : params.negative_prompt.c_str();
    gen_params.width = params.size.width;
    gen_params.height = params.size.height;
    gen_params.sample_params.sample_steps = params.steps;
    gen_params.sample_params.guidance.txt_cfg = params.cfg_scale;
    gen_params.sample_params.sample_method = static_cast<sample_method_t>(scheduler_to_sd_enum(params.scheduler));
    gen_params.seed = params.seed;
    gen_params.batch_count = params.batch_size;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Generate image (call C API with global namespace)
    sd_image_t* images = ::generate_image(ctx, &gen_params);

    auto end_time = std::chrono::high_resolution_clock::now();
    result.generation_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    if (!images) {
        result.error_message = "Image generation failed";
        return result;
    }

    // Copy image data to result
    result.image_size = params.size;
    for (int i = 0; i < params.batch_size; i++) {
        sd_image_t& img = images[i];
        if (img.data) {
            size_t data_size = img.width * img.height * img.channel;
            std::vector<uint8_t> img_data(img.data, img.data + data_size);
            result.images.push_back(std::move(img_data));
            free(img.data);
        }
    }
    free(images);

    result.success = !result.images.empty();

    std::cout << "[DiffusionBridge] Generation complete!" << std::endl;
    std::cout << "[DiffusionBridge] Time: " << result.generation_time_ms << " ms" << std::endl;
    std::cout << "[DiffusionBridge] Images generated: " << result.images.size() << std::endl;

    return result;
}

GenerationResult DiffusionBridge::img2img(
    const std::string& model_name,
    const std::string& input_image,
    const ImageGenerationParams& params
) {
    GenerationResult result;
    result.model_type = ModelType::IMAGE_DIFFUSION;

    // TODO: Load input image and pass to generation
    // This requires stb_image for loading

    result.error_message = "img2img not yet implemented";
    return result;
}

GenerationResult DiffusionBridge::generate_video(
    const std::string& model_name,
    const VideoGenerationParams& params
) {
    GenerationResult result;
    result.model_type = ModelType::VIDEO_DIFFUSION;

    std::lock_guard<std::mutex> lock(models_mutex_);

    // Check if model is loaded
    auto ctx_it = model_contexts_.find(model_name);
    if (ctx_it == model_contexts_.end() || !ctx_it->second) {
        result.error_message = "Model not loaded: " + model_name;
        return result;
    }

    sd_ctx_t* ctx = ctx_it->second;

    std::cout << "\n[DiffusionBridge] Generating video..." << std::endl;
    std::cout << "[DiffusionBridge] Prompt: " << params.prompt << std::endl;
    std::cout << "[DiffusionBridge] Frame size: " << params.frame_size.width << "x" << params.frame_size.height << std::endl;
    std::cout << "[DiffusionBridge] Frames: " << params.num_frames << std::endl;
    std::cout << "[DiffusionBridge] Steps: " << params.steps << std::endl;

    // Set up progress callback
    g_progress_callback = progress_callback_;
    g_gen_start = std::chrono::steady_clock::now();
    sd_set_progress_callback(sd_progress_bridge, nullptr);

    // Initialize video generation parameters
    sd_vid_gen_params_t vid_params;
    sd_vid_gen_params_init(&vid_params);

    vid_params.prompt = params.prompt.c_str();
    // Workaround: stable-diffusion.cpp has a bug with empty negative prompts causing tensor layout mismatch
    // Use a default negative prompt if none provided
    static const char* default_neg_prompt = "blurry, low quality, distorted, static, worst quality";

    // Always print to stdout for visibility
    std::cout << "[DiffusionBridge] Input negative_prompt: '" << params.negative_prompt << "'" << std::endl;
    std::cout << "[DiffusionBridge] Input is empty: " << (params.negative_prompt.empty() ? "YES" : "NO") << std::endl;

    vid_params.negative_prompt = params.negative_prompt.empty() ? default_neg_prompt : params.negative_prompt.c_str();

    std::cout << "[DiffusionBridge] Final negative_prompt: '" << vid_params.negative_prompt << "'" << std::endl;
    std::cout.flush();

    // Debug logging to file
    FILE* logf = fopen("D:/SnapLLM_Workspace/diffusion_debug.log", "a");
    if (logf) {
        fprintf(logf, "[DiffusionBridge] About to generate video:\n");
        fprintf(logf, "  Prompt: %s\n", params.prompt.c_str());
        fprintf(logf, "  Negative: %s\n", vid_params.negative_prompt);
        fprintf(logf, "  Size: %dx%d, Frames: %d, Steps: %d\n",
                params.frame_size.width, params.frame_size.height, params.num_frames, params.steps);
        fclose(logf);
    }
    vid_params.width = params.frame_size.width;
    vid_params.height = params.frame_size.height;
    vid_params.video_frames = params.num_frames;
    vid_params.sample_params.sample_steps = params.steps;
    vid_params.sample_params.guidance.txt_cfg = params.cfg_scale;
    vid_params.sample_params.sample_method = static_cast<sample_method_t>(scheduler_to_sd_enum(params.scheduler));
    vid_params.seed = params.seed;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Debug: Log before calling generate_video
    logf = fopen("D:/SnapLLM_Workspace/diffusion_debug.log", "a");
    if (logf) {
        fprintf(logf, "[DiffusionBridge] Calling generate_video()...\n");
        fclose(logf);
    }

    // Generate video (call C API with global namespace)
    int num_frames_out = 0;
    sd_image_t* frames = ::generate_video(ctx, &vid_params, &num_frames_out);

    // Debug: Log after generate_video returns
    logf = fopen("D:/SnapLLM_Workspace/diffusion_debug.log", "a");
    if (logf) {
        fprintf(logf, "[DiffusionBridge] generate_video() returned: frames=%p, num_frames=%d\n",
                (void*)frames, num_frames_out);
        fclose(logf);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.generation_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    if (!frames || num_frames_out == 0) {
        result.error_message = "Video generation failed";
        return result;
    }

    // Copy frame data to result
    result.image_size = params.frame_size;
    result.fps = params.fps;
    for (int i = 0; i < num_frames_out; i++) {
        sd_image_t& frame = frames[i];
        if (frame.data) {
            size_t data_size = frame.width * frame.height * frame.channel;
            std::vector<uint8_t> frame_data(frame.data, frame.data + data_size);
            result.frames.push_back(std::move(frame_data));
            free(frame.data);
        }
    }
    free(frames);

    result.success = !result.frames.empty();

    std::cout << "[DiffusionBridge] Video generation complete!" << std::endl;
    std::cout << "[DiffusionBridge] Time: " << result.generation_time_ms << " ms" << std::endl;
    std::cout << "[DiffusionBridge] Frames generated: " << result.frames.size() << std::endl;

    return result;
}

bool DiffusionBridge::apply_lora(
    const std::string& model_name,
    const std::string& lora_path,
    float strength
) {
    // LoRA is applied during context creation in stable-diffusion.cpp
    // Would need to reload model with lora_model_dir set
    std::cerr << "[DiffusionBridge] Dynamic LoRA loading not yet implemented" << std::endl;
    return false;
}

void DiffusionBridge::unload_model(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(models_mutex_);

    auto it = model_contexts_.find(model_name);
    if (it != model_contexts_.end()) {
        if (it->second) {
            free_sd_ctx(it->second);
        }
        model_contexts_.erase(it);
    }

    model_info_.erase(model_name);

    if (model_vram_usage_.count(model_name)) {
        total_vram_used_ -= model_vram_usage_[model_name];
        model_vram_usage_.erase(model_name);
    }

    std::cout << "[DiffusionBridge] Model '" << model_name << "' unloaded" << std::endl;
}

bool DiffusionBridge::is_model_loaded(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(models_mutex_);
    return model_contexts_.count(model_name) > 0 && model_contexts_.at(model_name) != nullptr;
}

std::vector<std::string> DiffusionBridge::get_loaded_models() const {
    std::lock_guard<std::mutex> lock(models_mutex_);
    std::vector<std::string> models;
    for (const auto& [name, ctx] : model_contexts_) {
        if (ctx) models.push_back(name);
    }
    return models;
}

const DiffusionModelInfo* DiffusionBridge::get_model_info(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(models_mutex_);
    auto it = model_info_.find(model_name);
    return it != model_info_.end() ? &it->second : nullptr;
}

void DiffusionBridge::set_progress_callback(ProgressCallback callback) {
    progress_callback_ = callback;
}

bool DiffusionBridge::save_image(
    const std::vector<uint8_t>& image_data,
    const ImageSize& size,
    const std::string& output_path
) {
    // Determine format from extension
    std::string ext = std::filesystem::path(output_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    int result = 0;
    if (ext == ".png") {
        result = stbi_write_png(output_path.c_str(), size.width, size.height, 3,
                                image_data.data(), size.width * 3);
    } else if (ext == ".jpg" || ext == ".jpeg") {
        result = stbi_write_jpg(output_path.c_str(), size.width, size.height, 3,
                                image_data.data(), 95);
    } else if (ext == ".bmp") {
        result = stbi_write_bmp(output_path.c_str(), size.width, size.height, 3,
                                image_data.data());
    } else {
        std::cerr << "[DiffusionBridge] Unsupported format: " << ext << std::endl;
        return false;
    }

    return result != 0;
}

bool DiffusionBridge::ensure_vram_space(size_t needed_mb) {
    if (total_vram_used_ + needed_mb <= VRAM_BUDGET_MB) {
        return true;
    }

    std::cout << "[DiffusionBridge] Need " << needed_mb << " MB, have "
              << (VRAM_BUDGET_MB - total_vram_used_) << " MB free, evicting..." << std::endl;

    while (total_vram_used_ + needed_mb > VRAM_BUDGET_MB && !model_contexts_.empty()) {
        std::string evicted = evict_lru_model();
        if (evicted.empty()) break;
    }

    return (total_vram_used_ + needed_mb <= VRAM_BUDGET_MB);
}

std::string DiffusionBridge::evict_lru_model() {
    // Simple eviction: remove first model (TODO: implement proper LRU)
    if (model_contexts_.empty()) return "";

    auto it = model_contexts_.begin();
    std::string model_name = it->first;

    std::cout << "[DiffusionBridge] Evicting model: " << model_name << std::endl;

    if (it->second) {
        free_sd_ctx(it->second);
    }
    model_contexts_.erase(it);
    model_info_.erase(model_name);

    if (model_vram_usage_.count(model_name)) {
        size_t freed = model_vram_usage_[model_name];
        total_vram_used_ -= freed;
        model_vram_usage_.erase(model_name);
        std::cout << "[DiffusionBridge] Freed " << freed << " MB VRAM" << std::endl;
    }

    return model_name;
}

std::string DiffusionBridge::detect_architecture(const std::string& model_path) {
    std::string lower_path = model_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);

    // Video models (Wan2)
    if (lower_path.find("wan2.2") != std::string::npos ||
        lower_path.find("wan2_2") != std::string::npos ||
        lower_path.find("wan22") != std::string::npos) return "wan22";
    if (lower_path.find("wan2.1") != std::string::npos ||
        lower_path.find("wan2_1") != std::string::npos ||
        lower_path.find("wan21") != std::string::npos ||
        lower_path.find("wan2") != std::string::npos) return "wan21";

    // Image models
    if (lower_path.find("flux") != std::string::npos) return "flux";
    if (lower_path.find("sd3.5") != std::string::npos ||
        lower_path.find("sd35") != std::string::npos) return "sd35";
    if (lower_path.find("sd3") != std::string::npos) return "sd3";
    if (lower_path.find("sdxl") != std::string::npos) return "sdxl";
    if (lower_path.find("sd2") != std::string::npos ||
        lower_path.find("sd_2") != std::string::npos ||
        lower_path.find("sd-2") != std::string::npos) return "sd21";

    // Default to SD 1.5
    return "sd15";
}

ImageSize DiffusionBridge::get_default_size(const std::string& architecture) {
    if (architecture == "flux") return {1024, 1024};
    if (architecture == "sd3" || architecture == "sd35") return {1024, 1024};
    if (architecture == "sdxl") return {1024, 1024};
    if (architecture == "sd21") return {768, 768};
    if (architecture == "wan21" || architecture == "wan22") return {832, 480}; // Video default
    return {512, 512};  // SD 1.5 default
}

int DiffusionBridge::scheduler_to_sd_enum(DiffusionScheduler scheduler) {
    switch (scheduler) {
        case DiffusionScheduler::EULER: return EULER_SAMPLE_METHOD;
        case DiffusionScheduler::EULER_A: return EULER_A_SAMPLE_METHOD;
        case DiffusionScheduler::HEUN: return HEUN_SAMPLE_METHOD;
        case DiffusionScheduler::DPM_PP_2M: return DPMPP2M_SAMPLE_METHOD;
        case DiffusionScheduler::LCM: return LCM_SAMPLE_METHOD;
        case DiffusionScheduler::DDIM: return DDIM_TRAILING_SAMPLE_METHOD;
        default: return EULER_A_SAMPLE_METHOD;
    }
}

} // namespace snapllm
