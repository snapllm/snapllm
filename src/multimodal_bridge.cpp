/**
 * @file multimodal_bridge.cpp
 * @brief Implementation of multimodal bridge using llama.cpp's mtmd library
 */

#include "snapllm/multimodal_bridge.h"

#ifdef SNAPLLM_HAS_MULTIMODAL

#include "llama.h"
#include "mtmd.h"
#include "mtmd-helper.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb_image.h"

#include <iostream>
#include <chrono>
#include <cstring>
#include <vector>
#include <mutex>

namespace snapllm {

struct MultimodalBridge::Impl {
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    mtmd_context* mtmd_ctx = nullptr;

    MultimodalConfig config;
    bool has_vision = false;
    bool has_audio = false;
    std::string image_marker;
    std::mutex mutex;

    ~Impl() {
        unload();
    }

    void unload() {
        if (mtmd_ctx) {
            mtmd_free(mtmd_ctx);
            mtmd_ctx = nullptr;
        }
        if (ctx) {
            llama_free(ctx);
            ctx = nullptr;
        }
        if (model) {
            llama_model_free(model);
            model = nullptr;
        }
        has_vision = false;
        has_audio = false;
    }
};

MultimodalBridge::MultimodalBridge() : impl_(std::make_unique<Impl>()) {}

MultimodalBridge::~MultimodalBridge() = default;

bool MultimodalBridge::load_model(const MultimodalConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    // Unload existing model
    impl_->unload();
    impl_->config = config;

    std::cout << "[MultimodalBridge] Loading multimodal model..." << std::endl;
    std::cout << "[MultimodalBridge] Model: " << config.model_path << std::endl;
    std::cout << "[MultimodalBridge] MMProj: " << config.mmproj_path << std::endl;

    // Initialize llama backend
    llama_backend_init();

    // Load the main LLM model
    llama_model_params model_params = llama_model_default_params();
    // Use 999 for "all layers" since -1 doesn't work as expected in llama.cpp
    model_params.n_gpu_layers = (config.n_gpu_layers < 0) ? 999 : config.n_gpu_layers;

    std::cout << "[MultimodalBridge] Loading LLM with n_gpu_layers=" << model_params.n_gpu_layers << std::endl;

    impl_->model = llama_model_load_from_file(config.model_path.c_str(), model_params);
    if (!impl_->model) {
        std::cerr << "[MultimodalBridge] Failed to load model: " << config.model_path << std::endl;
        return false;
    }

    std::cout << "[MultimodalBridge] LLM model loaded successfully" << std::endl;

    // Create llama context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = config.ctx_size;
    ctx_params.n_threads = config.n_threads;
    ctx_params.n_threads_batch = config.n_threads;

    impl_->ctx = llama_init_from_model(impl_->model, ctx_params);
    if (!impl_->ctx) {
        std::cerr << "[MultimodalBridge] Failed to create llama context" << std::endl;
        impl_->unload();
        return false;
    }

    std::cout << "[MultimodalBridge] LLM context created" << std::endl;

    // Initialize mtmd (multimodal) context
    mtmd_context_params mtmd_params = mtmd_context_params_default();
    mtmd_params.use_gpu = config.use_gpu;
    mtmd_params.n_threads = config.n_threads;
    mtmd_params.verbosity = GGML_LOG_LEVEL_INFO;

    std::cout << "[MultimodalBridge] Initializing mtmd context..." << std::endl;

    impl_->mtmd_ctx = mtmd_init_from_file(
        config.mmproj_path.c_str(),
        impl_->model,
        mtmd_params
    );

    std::cout << "[MultimodalBridge] mtmd_init_from_file returned" << std::endl;

    if (!impl_->mtmd_ctx) {
        std::cerr << "[MultimodalBridge] Failed to load multimodal projector: " << config.mmproj_path << std::endl;
        impl_->unload();
        return false;
    }

    std::cout << "[MultimodalBridge] Multimodal projector loaded" << std::endl;

    // Check capabilities - with null checks
    if (impl_->mtmd_ctx) {
        impl_->has_vision = mtmd_support_vision(impl_->mtmd_ctx);
        impl_->has_audio = mtmd_support_audio(impl_->mtmd_ctx);
        const char* marker = mtmd_default_marker();
        impl_->image_marker = marker ? marker : "<image>";
    }

    std::cout << "[MultimodalBridge] Vision support: " << (impl_->has_vision ? "yes" : "no") << std::endl;
    std::cout << "[MultimodalBridge] Audio support: " << (impl_->has_audio ? "yes" : "no") << std::endl;
    std::cout << "[MultimodalBridge] Image marker: " << impl_->image_marker << std::endl;

    return true;
}

bool MultimodalBridge::is_loaded() const {
    return impl_->model != nullptr && impl_->ctx != nullptr && impl_->mtmd_ctx != nullptr;
}

void MultimodalBridge::unload() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->unload();
}

bool MultimodalBridge::supports_vision() const {
    return impl_->has_vision;
}

bool MultimodalBridge::supports_audio() const {
    return impl_->has_audio;
}

std::string MultimodalBridge::get_image_marker() const {
    return impl_->image_marker;
}

std::string MultimodalBridge::get_model_info() const {
    if (!impl_->model) return "No model loaded";

    char buf[256];
    llama_model_desc(impl_->model, buf, sizeof(buf));
    return buf;
}

ImageInput MultimodalBridge::load_image(const std::string& path) {
    ImageInput input;
    input.path = path;

    int width, height, channels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 3);

    if (!data) {
        std::cerr << "[MultimodalBridge] Failed to load image: " << path << std::endl;
        return input;
    }

    input.width = width;
    input.height = height;
    input.data.assign(data, data + (width * height * 3));

    stbi_image_free(data);

    std::cout << "[MultimodalBridge] Loaded image: " << path
              << " (" << width << "x" << height << ")" << std::endl;

    return input;
}

// Helper function to decode a single token during generation
static bool decode_single_token(llama_context* ctx, llama_token token, llama_pos& pos) {
    llama_batch batch = llama_batch_init(1, 0, 1);
    batch.n_tokens = 1;
    batch.token[0] = token;
    batch.pos[0] = pos++;
    batch.n_seq_id[0] = 1;
    batch.seq_id[0][0] = 0;
    batch.logits[0] = true;

    int ret = llama_decode(ctx, batch);
    llama_batch_free(batch);

    return ret == 0;
}

MultimodalResult MultimodalBridge::generate(
    const std::string& prompt,
    const std::vector<ImageInput>& images,
    int max_tokens,
    TokenCallback callback
) {
    MultimodalSamplingParams sampling;
    return generate(prompt, images, sampling, max_tokens, callback);
}

MultimodalResult MultimodalBridge::generate(
    const std::string& prompt,
    const std::vector<ImageInput>& images,
    const MultimodalSamplingParams& sampling,
    int max_tokens,
    TokenCallback callback
) {
    MultimodalResult result;

    std::lock_guard<std::mutex> lock(impl_->mutex);

    std::cout << "[MultimodalBridge] Starting generate()" << std::endl;

    if (!is_loaded()) {
        result.error_message = "Model not loaded";
        return result;
    }

    if (!impl_->has_vision && !images.empty()) {
        result.error_message = "Model does not support vision input";
        return result;
    }

    auto start_encode = std::chrono::high_resolution_clock::now();

    // Create bitmaps from images
    std::cout << "[MultimodalBridge] Creating bitmaps for " << images.size() << " images" << std::endl;
    std::vector<mtmd_bitmap*> bitmaps;
    for (const auto& img : images) {
        if (img.data.empty()) {
            result.error_message = "Empty image data";
            for (auto* bmp : bitmaps) mtmd_bitmap_free(bmp);
            return result;
        }

        mtmd_bitmap* bmp = mtmd_bitmap_init(
            img.width, img.height,
            img.data.data()
        );
        if (bmp) {
            bitmaps.push_back(bmp);
            std::cout << "[MultimodalBridge] Created bitmap " << bitmaps.size()
                      << " (" << img.width << "x" << img.height << ")" << std::endl;
        } else {
            std::cerr << "[MultimodalBridge] Failed to create bitmap" << std::endl;
        }
    }

    // Prepare input text (apply chat template if available)
    std::string formatted_prompt = prompt;
    const char* chat_template = llama_model_chat_template(impl_->model, nullptr);
    if (chat_template) {
        llama_chat_message messages[] = {
            { "user", prompt.c_str() }
        };
        std::vector<char> buf(prompt.size() * 4 + 512);
        int32_t applied = llama_chat_apply_template(
            chat_template, messages, 1, true, buf.data(), buf.size()
        );
        if (applied > 0 && applied < static_cast<int32_t>(buf.size())) {
            formatted_prompt.assign(buf.data(), applied);
        }
    }

    std::cout << "[MultimodalBridge] Tokenizing prompt: " << formatted_prompt.substr(0, 50) << "..." << std::endl;
    mtmd_input_text input_text;
    input_text.text = formatted_prompt.c_str();
    input_text.add_special = true;
    input_text.parse_special = true;

    // Tokenize with images
    mtmd_input_chunks* chunks = mtmd_input_chunks_init();
    if (!chunks) {
        result.error_message = "Failed to initialize input chunks";
        for (auto* bmp : bitmaps) mtmd_bitmap_free(bmp);
        return result;
    }

    std::vector<const mtmd_bitmap*> bitmap_ptrs(bitmaps.begin(), bitmaps.end());

    std::cout << "[MultimodalBridge] Calling mtmd_tokenize with " << bitmap_ptrs.size() << " bitmaps" << std::endl;
    int32_t tokenize_result = mtmd_tokenize(
        impl_->mtmd_ctx,
        chunks,
        &input_text,
        bitmap_ptrs.empty() ? nullptr : bitmap_ptrs.data(),
        bitmap_ptrs.size()
    );

    // Free bitmaps after tokenization
    for (auto* bmp : bitmaps) {
        mtmd_bitmap_free(bmp);
    }
    bitmaps.clear();

    if (tokenize_result != 0) {
        mtmd_input_chunks_free(chunks);
        result.error_message = "Failed to tokenize input (error " + std::to_string(tokenize_result) + ")";
        if (tokenize_result == 1) {
            result.error_message += ": number of images doesn't match markers";
        } else if (tokenize_result == 2) {
            result.error_message += ": image preprocessing failed";
        }
        std::cerr << "[MultimodalBridge] " << result.error_message << std::endl;
        return result;
    }

    std::cout << "[MultimodalBridge] Tokenization successful" << std::endl;

    // Clear KV cache
    llama_memory_clear(llama_get_memory(impl_->ctx), true);

    // Evaluate all chunks using mtmd helper (handles batching + M-RoPE)
    llama_pos n_past = 0;
    llama_pos new_n_past = 0;
    int32_t n_batch = static_cast<int32_t>(llama_n_batch(impl_->ctx));
    if (n_batch <= 0) {
        n_batch = 512;
    }

    std::cout << "[MultimodalBridge] Evaluating chunks with n_batch=" << n_batch << std::endl;
    int32_t eval_result = mtmd_helper_eval_chunks(
        impl_->mtmd_ctx,
        impl_->ctx,
        chunks,
        n_past,
        0,
        n_batch,
        true,
        &new_n_past
    );

    mtmd_input_chunks_free(chunks);

    if (eval_result != 0) {
        result.error_message = "Failed to evaluate multimodal chunks (error " + std::to_string(eval_result) + ")";
        return result;
    }

    n_past = new_n_past;

    auto end_encode = std::chrono::high_resolution_clock::now();
    result.encoding_time_ms = std::chrono::duration<double, std::milli>(end_encode - start_encode).count();

    std::cout << "[MultimodalBridge] Encoding time: " << result.encoding_time_ms << " ms" << std::endl;

    // Generate tokens
    auto start_gen = std::chrono::high_resolution_clock::now();

    const llama_vocab* vocab = llama_model_get_vocab(impl_->model);
    llama_token eos_token = llama_vocab_eos(vocab);

    std::string response;
    int tokens_generated = 0;

    std::cout << "[MultimodalBridge] Starting token generation (max=" << max_tokens << ")..." << std::endl;

    // Initialize sampler chain
    llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    sparams.no_perf = false;
    llama_sampler* smpl = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(smpl, llama_sampler_init_penalties(
        max_tokens,
        sampling.repeat_penalty,
        0.0f,
        0.0f
    ));
    if (sampling.top_k > 0) {
        llama_sampler_chain_add(smpl, llama_sampler_init_top_k(sampling.top_k));
    }
    if (sampling.top_p > 0.0f && sampling.top_p < 1.0f) {
        llama_sampler_chain_add(smpl, llama_sampler_init_top_p(sampling.top_p, 1));
    }
    if (sampling.temperature > 0.0f) {
        llama_sampler_chain_add(smpl, llama_sampler_init_temp(sampling.temperature));
    }
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(0));

    for (int i = 0; i < max_tokens; i++) {
        // Sample next token from current logits
        llama_token next_token = llama_sampler_sample(smpl, impl_->ctx, -1);

        // Check for EOS
        if (next_token == eos_token) {
            break;
        }

        // Decode token to string
        char buf[256];
        int len = llama_token_to_piece(vocab, next_token, buf, sizeof(buf), 0, true);
        if (len > 0) {
            std::string token_str(buf, len);
            response += token_str;
            tokens_generated++;

            // Call callback if provided
            if (callback) {
                if (!callback(token_str)) {
                    break;  // Stop if callback returns false
                }
            }
        }

        // Decode next token
        if (!decode_single_token(impl_->ctx, next_token, n_past)) {
            result.error_message = "Failed to decode generated token";
            llama_sampler_free(smpl);
            return result;
        }
    }

    llama_sampler_free(smpl);

    auto end_gen = std::chrono::high_resolution_clock::now();
    result.generation_time_ms = std::chrono::duration<double, std::milli>(end_gen - start_gen).count();

    result.success = true;
    result.response = response;
    result.tokens_generated = tokens_generated;

    if (result.generation_time_ms > 0) {
        result.tokens_per_second = (tokens_generated * 1000.0f) / result.generation_time_ms;
    }

    std::cout << "[MultimodalBridge] Generated " << tokens_generated << " tokens in "
              << result.generation_time_ms << " ms (" << result.tokens_per_second << " tok/s)" << std::endl;

    return result;
}

MultimodalResult MultimodalBridge::generate_text(
    const std::string& prompt,
    int max_tokens,
    TokenCallback callback
) {
    return generate(prompt, {}, max_tokens, callback);
}

} // namespace snapllm

#else // !SNAPLLM_HAS_MULTIMODAL

// Stub implementation when multimodal is disabled
namespace snapllm {

struct MultimodalBridge::Impl {};

MultimodalBridge::MultimodalBridge() : impl_(std::make_unique<Impl>()) {}
MultimodalBridge::~MultimodalBridge() = default;

bool MultimodalBridge::load_model(const MultimodalConfig&) {
    std::cerr << "[MultimodalBridge] Multimodal support not compiled\n";
    return false;
}

bool MultimodalBridge::is_loaded() const { return false; }
void MultimodalBridge::unload() {}
bool MultimodalBridge::supports_vision() const { return false; }
bool MultimodalBridge::supports_audio() const { return false; }
std::string MultimodalBridge::get_image_marker() const { return ""; }
std::string MultimodalBridge::get_model_info() const { return "Not available"; }
ImageInput MultimodalBridge::load_image(const std::string&) { return {}; }

MultimodalResult MultimodalBridge::generate(
    const std::string&, const std::vector<ImageInput>&, int, TokenCallback
) {
    MultimodalResult r;
    r.error_message = "Multimodal support not compiled";
    return r;
}

MultimodalResult MultimodalBridge::generate_text(
    const std::string&, int, TokenCallback
) {
    MultimodalResult r;
    r.error_message = "Multimodal support not compiled";
    return r;
}

} // namespace snapllm

#endif // SNAPLLM_HAS_MULTIMODAL
