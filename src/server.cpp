/**
 * @file server.cpp
 * @brief SnapLLM HTTP Server Implementation
 *
 * OpenAI-compatible REST API server for LLM inference.
 * Uses cpp-httplib for HTTP and nlohmann/json for JSON parsing.
 */

// httplib configuration - MUST come before including httplib.h
#define CPPHTTPLIB_FORM_URL_ENCODED_PAYLOAD_MAX_LENGTH 1048576  // 1MB
#define CPPHTTPLIB_LISTEN_BACKLOG 512
#define CPPHTTPLIB_TCP_NODELAY true
#define CPPHTTPLIB_THREAD_POOL_COUNT 8  // Cap HTTP threads (inference serialized by semaphore)

// Windows socket headers need to come before httplib on Windows
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "snapllm/server.h"
#include "snapllm/server_limits.h"
#include "snapllm/server_security.h"
#include "snapllm/version.h"
#include "snapllm/websocket.h"
#include "snapllm/workspace_paths.h"
#include "snapllm/model_types.h"
#include "snapllm/request_router.h"

#include <filesystem>
namespace fs = std::filesystem;

#ifdef SNAPLLM_HAS_DIFFUSION
#include "snapllm/diffusion_bridge.h"
#endif

#ifdef SNAPLLM_HAS_MULTIMODAL
#include "snapllm/multimodal_bridge.h"
#include "stb_image.h"  // For decoding base64 images in vision endpoint
#include "gguf.h"
#endif

// Include vendor libraries
#include "cpp-httplib/httplib.h"
#include "nlohmann/json.hpp"

#include <iostream>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <cstring>
#include <cctype>
#include <fstream>
#include <limits>
#include <cmath>
#include <cstdio>
#include <vector>
#include <thread>

using json = nlohmann::ordered_json;

namespace snapllm {
namespace {

bool read_bounded_integer(
    const json& object,
    const char* field,
    std::int64_t default_value,
    std::int64_t minimum,
    std::int64_t maximum,
    int& result,
    std::string& error) {
    std::int64_t value = default_value;
    if (object.contains(field)) {
        const json& input = object[field];
        if (input.is_number_unsigned()) {
            const auto unsigned_value = input.get<std::uint64_t>();
            if (unsigned_value >
                static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
                error = std::string("'") + field + "' is outside the supported integer range";
                return false;
            }
            value = static_cast<std::int64_t>(unsigned_value);
        } else if (input.is_number_integer()) {
            value = input.get<std::int64_t>();
        } else {
            error = std::string("'") + field + "' must be an integer";
            return false;
        }
    }
    if (value < minimum || value > maximum) {
        error = std::string("'") + field + "' must be between " +
                std::to_string(minimum) + " and " + std::to_string(maximum);
        return false;
    }
    result = static_cast<int>(value);
    return true;
}

bool read_bounded_float(
    const json& object,
    const char* field,
    double default_value,
    double minimum,
    double maximum,
    float& result,
    std::string& error) {
    double value = default_value;
    if (object.contains(field)) {
        const json& input = object[field];
        if (!input.is_number()) {
            error = std::string("'") + field + "' must be a number";
            return false;
        }
        value = input.get<double>();
    }
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        error = std::string("'") + field + "' must be finite and between " +
            std::to_string(minimum) + " and " + std::to_string(maximum);
        return false;
    }
    result = static_cast<float>(value);
    return true;
}

bool read_bounded_string(
    const json& object,
    const char* field,
    std::string_view default_value,
    std::size_t maximum_bytes,
    bool allow_empty,
    std::string& result,
    std::string& error) {
    if (!object.contains(field)) {
        result.assign(default_value);
        if ((!allow_empty && result.empty()) || result.size() > maximum_bytes) {
            error = std::string("'") + field + "' has an invalid length";
            return false;
        }
        return true;
    }
    if (!object[field].is_string()) {
        error = std::string("'") + field + "' must be a string";
        return false;
    }
    result = object[field].get<std::string>();
    if ((!allow_empty && result.empty()) || result.size() > maximum_bytes) {
        error = std::string("'") + field + "' has an invalid length";
        return false;
    }
    return true;
}

bool read_bounded_string_alias(
    const json& object,
    const char* primary_field,
    const char* fallback_field,
    std::string_view default_value,
    std::size_t maximum_bytes,
    bool allow_empty,
    std::string& result,
    std::string& error) {
    if (object.contains(primary_field)) {
        return read_bounded_string(
            object, primary_field, default_value, maximum_bytes,
            allow_empty, result, error);
    }
    return read_bounded_string(
        object, fallback_field, default_value, maximum_bytes,
        allow_empty, result, error);
}

bool validate_text_messages(
    const json& messages,
    std::string& error,
    std::size_t& total_text_bytes) {
    if (!messages.is_array() ||
        !limits::is_valid_message_count(messages.size())) {
        error = "'messages' must contain between 1 and " +
                std::to_string(limits::kMaximumMessages) + " entries";
        return false;
    }

    total_text_bytes = 0;
    for (const auto& message : messages) {
        if (!message.is_object() || !message.contains("content") ||
            !message["content"].is_string()) {
            error = "Each message must be an object with string 'content'";
            return false;
        }
        if (message.contains("role") && !message["role"].is_string()) {
            error = "Each message 'role' must be a string";
            return false;
        }
        const std::size_t content_bytes =
            message["content"].get_ref<const std::string&>().size();
        if (!limits::is_valid_message_size(content_bytes, true) ||
            total_text_bytes > limits::kMaximumPromptBytes - content_bytes) {
            error = "Message content exceeds the request limits";
            return false;
        }
        total_text_bytes += content_bytes;
    }
    return true;
}

std::vector<fs::path> request_path_roots(const ServerConfig& config) {
    return {
        fs::path(config.default_models_path),
        fs::path(config.workspace_root)
    };
}

int hex_digit_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::optional<std::string> decode_model_path_component(std::string_view encoded) {
    if (encoded.empty() || encoded.size() > limits::kMaximumStringBytes) {
        return std::nullopt;
    }
    std::string decoded;
    decoded.reserve(encoded.size());
    for (size_t index = 0; index < encoded.size(); ++index) {
        unsigned char value = static_cast<unsigned char>(encoded[index]);
        if (value == '%') {
            if (index + 2 >= encoded.size()) return std::nullopt;
            const int high = hex_digit_value(encoded[index + 1]);
            const int low = hex_digit_value(encoded[index + 2]);
            if (high < 0 || low < 0) return std::nullopt;
            value = static_cast<unsigned char>((high << 4) | low);
            index += 2;
        } else if (value == '+') {
            value = ' ';
        }
        if (value < 0x20 || value == 0x7f || value == '/' || value == '\\') {
            return std::nullopt;
        }
        decoded.push_back(static_cast<char>(value));
    }
    return !limits::is_valid_identifier_component(decoded) ? std::nullopt
                           : std::optional<std::string>(std::move(decoded));
}

}  // namespace

// Get default workspace path based on OS
static std::string get_default_workspace() {
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

// Get default models path based on OS
static std::string get_default_models_path() {
    const char* env_models = std::getenv("SNAPLLM_MODELS_PATH");
    if (env_models && std::strlen(env_models) > 0) {
        return std::string(env_models);
    }
#ifdef _WIN32
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile) {
        return std::string(userprofile) + "\\Models";
    }
    return "C:\\Models";
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/Models";
    }
    return "/tmp/Models";
#endif
}

static std::string get_default_config_path() {
    const char* env_config = std::getenv("SNAPLLM_CONFIG_PATH");
    if (env_config && std::strlen(env_config) > 0) {
        return std::string(env_config);
    }
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        return std::string(appdata) + "\\SnapLLM\\config.json";
    }
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile) {
        return std::string(userprofile) + "\\SnapLLM\\config.json";
    }
    return "C:\\SnapLLM\\config.json";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg) {
        return std::string(xdg) + "/snapllm/config.json";
    }
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/snapllm/config.json";
    }
    return "/tmp/snapllm/config.json";
#endif
}

// Resolve workspace root (use default if empty)
static std::string resolve_workspace(const std::string& workspace_root) {
    return workspace_root.empty() ? get_default_workspace() : workspace_root;
}

static std::string http_origin_for(const std::string& host, int port) {
    std::string authority = host;
    if (authority.find(':') != std::string::npos &&
        !(authority.front() == '[' && authority.back() == ']')) {
        authority = "[" + authority + "]";
    }
    return "http://" + authority + ":" + std::to_string(port);
}

static bool is_safe_host_header(std::string_view value) {
    if (value.empty() || value.size() > 512) {
        return false;
    }
    for (char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x21U || byte > 0x7eU ||
            character == '/' || character == '\\' ||
            character == '@' || character == '?' || character == '#') {
            return false;
        }
    }
    return true;
}

static bool open_url_without_shell(const std::string& url) {
#ifdef _WIN32
    const auto result = reinterpret_cast<std::intptr_t>(
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
#else
    const pid_t child = fork();
    if (child < 0) {
        return false;
    }
    if (child == 0) {
#ifdef __APPLE__
        execlp("open", "open", url.c_str(), static_cast<char*>(nullptr));
#else
        execlp("xdg-open", "xdg-open", url.c_str(), static_cast<char*>(nullptr));
#endif
        _exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) {
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

static json build_persisted_config(const ServerConfig& config) {
    return json{
        {"schema_version", 1},
        {"server", {
            {"host", config.host},
            {"port", config.port},
            {"cors_enabled", config.cors_enabled},
            {"timeout_seconds", config.timeout_seconds},
            {"max_concurrent_requests", config.max_concurrent_requests},
            {"max_active_inferences", config.max_active_inferences}
        }},
        {"workspace", {
            {"root", resolve_workspace(config.workspace_root)},
            {"default_models_path", config.default_models_path.empty() ? get_default_models_path() : config.default_models_path}
        }},
        {"runtime", {
            {"max_models", config.max_models},
            {"default_ram_budget_mb", config.default_ram_budget_mb},
            {"default_strategy", config.default_strategy},
            {"enable_gpu", config.enable_gpu}
        }}
    };
}

static bool write_config_file(const std::string& path, const json& payload, std::string& error) {
    static std::mutex config_write_mutex;
    std::lock_guard<std::mutex> write_lock(config_write_mutex);
    fs::path temp_path;
    try {
        fs::path target(path);
        if (target.has_parent_path()) {
            fs::create_directories(target.parent_path());
        }
        std::random_device random;
        temp_path = target;
        temp_path += ".tmp." + std::to_string(random()) + "." +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());

        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Failed to open config file for writing";
            return false;
        }
        const std::string serialized = payload.dump(2);
        out.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        out.flush();
        if (!out) {
            out.close();
            fs::remove(temp_path);
            error = "Failed to write complete config file";
            return false;
        }
        out.close();
        if (!out) {
            fs::remove(temp_path);
            error = "Failed to close config file";
            return false;
        }

#ifdef _WIN32
        if (!MoveFileExW(
                temp_path.wstring().c_str(),
                target.wstring().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto code = GetLastError();
            fs::remove(temp_path);
            error = "Failed to replace config file (Windows error " +
                std::to_string(code) + ")";
            return false;
        }
#else
        if (std::rename(temp_path.c_str(), target.c_str()) != 0) {
            const int code = errno;
            fs::remove(temp_path);
            error = "Failed to replace config file: " +
                std::error_code(code, std::generic_category()).message();
            return false;
        }
#endif
        return true;
    } catch (const std::exception& e) {
        if (!temp_path.empty()) {
            std::error_code ignored;
            fs::remove(temp_path, ignored);
        }
        error = e.what();
        return false;
    }
}

// Constants
static constexpr const char* MIMETYPE_JSON = "application/json; charset=utf-8";
static constexpr const char* MIMETYPE_SSE = "text/event-stream";
// Shared DiffusionBridge instance (initialized lazily)
#ifdef SNAPLLM_HAS_DIFFUSION
static DiffusionBridge* get_diffusion_bridge(const std::string& workspace_root) {
    static std::unique_ptr<DiffusionBridge> instance;
    static std::once_flag flag;
    std::call_once(flag, [&]() {
        instance = std::make_unique<DiffusionBridge>(
            (std::filesystem::path(workspace_root) / "diffusion").string());
    });
    return instance.get();
}
#endif

// Shared MultimodalBridge instance (initialized lazily)
#ifdef SNAPLLM_HAS_MULTIMODAL
static MultimodalBridge* get_multimodal_bridge() {
    static std::unique_ptr<MultimodalBridge> instance;
    static std::once_flag flag;
    std::call_once(flag, []() {
        instance = std::make_unique<MultimodalBridge>();
    });
    return instance.get();
}

#ifdef SNAPLLM_HAS_MULTIMODAL
static const std::vector<std::string>& get_supported_projector_types() {
    static const std::vector<std::string> types = {
        "mlp",
        "ldp",
        "ldpv2",
        "resampler",
        "adapter",
        "qwen2vl_merger",
        "qwen2.5vl_merger",
        "gemma3",
        "idefics3",
        "pixtral",
        "ultravox",
        "internvl",
        "llama4",
        "qwen2a",
        "qwen2.5o",
        "voxtral",
        "lfm2",
        "kimivl",
        "lightonocr"
    };
    return types;
}

static bool is_supported_projector_type(const std::string& proj_type) {
    const auto& types = get_supported_projector_types();
    return std::find(types.begin(), types.end(), proj_type) != types.end();
}

static std::string format_supported_projector_types() {
    const auto& types = get_supported_projector_types();
    std::ostringstream oss;
    for (size_t i = 0; i < types.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << types[i];
    }
    return oss.str();
}

static std::string read_mmproj_projector_type(const std::string& mmproj_path) {
    if (mmproj_path.empty()) {
        return "";
    }

    gguf_init_params params{};
    params.no_alloc = true;
    params.ctx = nullptr;

    gguf_context* ctx = gguf_init_from_file(mmproj_path.c_str(), params);
    if (!ctx) {
        return "";
    }

    std::string result;
    int64_t key_id = gguf_find_key(ctx, "clip.projector_type");
    if (key_id < 0) {
        key_id = gguf_find_key(ctx, "clip.vision.projector_type");
    }
    if (key_id >= 0 && gguf_get_kv_type(ctx, key_id) == GGUF_TYPE_STRING) {
        const char* val = gguf_get_val_str(ctx, key_id);
        if (val) {
            result = val;
        }
    }

    gguf_free(ctx);
    return result;
}
#endif
#endif

// ============================================================================
// Constructor / Destructor
// ============================================================================

SnapLLMServer::SnapLLMServer(const ServerConfig& config)
    : config_(config)
    , manager_(std::make_shared<ModelManager>(resolve_workspace(config.workspace_root)))
    , workspace_paths_(WorkspacePaths::from_home(fs::path(resolve_workspace(config.workspace_root))))
    , svr_(std::make_unique<httplib::Server>())
    , start_time_(std::chrono::steady_clock::now())
{
    if (!security::is_valid_bind_host(config_.host)) {
        throw std::invalid_argument("Invalid server bind host");
    }
    if (config_.port < 1 || config_.port > 65535) {
        throw std::invalid_argument("Server port must be between 1 and 65535");
    }
    if (!config_.api_key.empty() && !security::meets_api_key_policy(config_.api_key)) {
        throw std::invalid_argument(
            "SNAPLLM_API_KEY must contain 32-4096 visible ASCII characters");
    }
    if (!security::is_loopback_host(config_.host) && config_.api_key.empty()) {
        throw std::invalid_argument(
            "A strong API key is required when binding SnapLLM beyond loopback");
    }
    const char* network_guard = std::getenv("SNAPLLM_NETWORK_GUARD");
    if (!security::is_loopback_host(config_.host) &&
        (!network_guard || !security::is_valid_network_guard(network_guard))) {
        throw std::invalid_argument(
            "Non-loopback binds require SNAPLLM_NETWORK_GUARD=reverse-proxy "
            "or loopback-port-publish");
    }
    if (config_.max_payload_bytes == 0 ||
        config_.max_payload_bytes > 256ULL * 1024ULL * 1024ULL) {
        throw std::invalid_argument("HTTP payload limit must be between 1 byte and 256 MiB");
    }
    if (config_.max_concurrent_requests < 1 ||
        config_.max_concurrent_requests > 128) {
        throw std::invalid_argument("HTTP worker count must be between 1 and 128");
    }
    const size_t http_workers = static_cast<size_t>(config_.max_concurrent_requests);
    // Keep admission bounded, but allow a normal burst to drain behind the
    // serialized GPU inference slot. Requests beyond this queue are rejected
    // instead of consuming unbounded memory.
    const size_t maximum_queued_requests = std::min<size_t>(http_workers * 8, 256);
    svr_->new_task_queue = [http_workers, maximum_queued_requests] {
        return new httplib::ThreadPool(http_workers, maximum_queued_requests);
    };

    std::vector<std::string> effective_origins{
        http_origin_for(config_.host, config_.port)
    };
    if (security::is_loopback_host(config_.host)) {
        effective_origins.push_back("tauri://localhost");
        effective_origins.push_back("https://tauri.localhost");
    }
    if (config_.cors_enabled) {
        effective_origins.insert(
            effective_origins.end(),
            config_.allowed_origins.begin(),
            config_.allowed_origins.end());
    }
    config_.allowed_origins = std::move(effective_origins);

    svr_->set_payload_max_length(config_.max_payload_bytes);
    // Bound receipt of unauthenticated headers/bodies independently from
    // potentially long inference and response-write durations.
    constexpr time_t kRequestReadTimeoutSeconds = 2;
    svr_->set_read_timeout(kRequestReadTimeoutSeconds, 0);
    svr_->set_write_timeout(config_.timeout_seconds, 0);
    svr_->set_idle_interval(5, 0);

    // Resolve workspace_root if empty
    if (config_.workspace_root.empty()) {
        config_.workspace_root = resolve_workspace(config.workspace_root);
    }
    if (config_.config_path.empty()) {
        config_.config_path = get_default_config_path();
    }
    if (config_.default_models_path.empty()) {
        config_.default_models_path = get_default_models_path();
    }
    // Ensure workspace directories exist
    try {
        for (const auto& dir : workspace_paths_.get_required_directories()) {
            fs::create_directories(dir);
        }
    } catch (const std::exception& e) {
        std::cerr << "[SnapLLM Server] Warning: Failed to create workspace directories: "
                  << e.what() << std::endl;
    }
    // Initialize context manager (vPID L2)
    context_manager_ = std::make_unique<ContextManager>(manager_.get(), workspace_paths_);

    // Configure inference concurrency limits.  Keep the conservative serial
    // default for GPU safety, but allow an explicit deployment opt-in so a
    // server with enough VRAM can serve multiple independent contexts.
    max_active_inferences_ = (std::max)(1, (std::min)(config_.max_active_inferences,
                                                       config_.max_concurrent_requests));
    if (const char* configured = std::getenv("SNAPLLM_MAX_ACTIVE_INFERENCES")) {
        try {
            const long parsed = std::stol(configured);
            if (parsed >= 1 && parsed <= config_.max_concurrent_requests) {
                max_active_inferences_ = static_cast<int>(parsed);
            } else {
                std::cerr << "[SnapLLM] Ignoring SNAPLLM_MAX_ACTIVE_INFERENCES outside 1.."
                          << config_.max_concurrent_requests << std::endl;
            }
        } catch (const std::exception&) {
            std::cerr << "[SnapLLM] Ignoring invalid SNAPLLM_MAX_ACTIVE_INFERENCES" << std::endl;
        }
    }
    std::cout << "[SnapLLM] HTTP inference gate: max " << max_active_inferences_ << " concurrent" << std::endl;

    // Also configure VPIDBridge-level semaphore as a safety net
    if (auto bridge = manager_->get_bridge()) {
        bridge->set_max_concurrent_inferences(max_active_inferences_);
    }

    setup_middleware();
    setup_routes();
}

SnapLLMServer::~SnapLLMServer() {
    stop();
}

// ============================================================================
// Inference Gate: HTTP-level concurrency control
// Prevents GPU OOM by limiting how many requests enter inference simultaneously.
// Requests that can't acquire a slot within timeout get HTTP 503.
// ============================================================================

bool SnapLLMServer::acquire_inference_gate(int timeout_ms) {
    waiting_inference_count_.fetch_add(1, std::memory_order_relaxed);
    std::unique_lock<std::mutex> lock(inference_gate_mutex_);
    bool acquired = inference_gate_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
        return active_inference_count_ < max_active_inferences_;
    });
    waiting_inference_count_.fetch_sub(1, std::memory_order_relaxed);
    if (acquired) {
        active_inference_count_++;
        std::cout << "[SnapLLM Gate] Acquired inference slot (" << active_inference_count_
                  << "/" << max_active_inferences_ << " active)" << std::endl;
    }
    return acquired;
}

void SnapLLMServer::release_inference_gate() {
    {
        std::lock_guard<std::mutex> lock(inference_gate_mutex_);
        active_inference_count_--;
        std::cout << "[SnapLLM Gate] Released inference slot (" << active_inference_count_
                  << "/" << max_active_inferences_ << " active)" << std::endl;
    }
    inference_gate_cv_.notify_one();
}

// RAII guard for inference gate - ensures release on all exit paths
namespace {
class InferenceGateGuard {
    SnapLLMServer* server_;
    bool acquired_;
    std::function<void()> release_fn_;
public:
    InferenceGateGuard(bool acquired, std::function<void()> release_fn)
        : acquired_(acquired), release_fn_(std::move(release_fn)) {}
    ~InferenceGateGuard() { if (acquired_) release_fn_(); }
    bool acquired() const { return acquired_; }
    // Prevent double-release
    InferenceGateGuard(const InferenceGateGuard&) = delete;
    InferenceGateGuard& operator=(const InferenceGateGuard&) = delete;
};
} // anonymous namespace

void SnapLLMServer::record_model_metrics(const std::string& model_id,
                                         uint64_t tokens_generated,
                                         double latency_ms,
                                         uint64_t request_count) {
    if (model_id.empty()) return;
    std::lock_guard<std::mutex> lock(model_metrics_mutex_);
    auto& metrics = model_metrics_[model_id];
    metrics.requests += request_count;
    metrics.tokens_generated += tokens_generated;
    metrics.total_latency_ms += latency_ms;
}

void SnapLLMServer::model_request_started(const std::string& model_id) {
    if (model_id.empty()) return;
    std::lock_guard<std::mutex> lock(model_metrics_mutex_);
    ++model_metrics_[model_id].in_flight;
}

void SnapLLMServer::model_request_finished(const std::string& model_id) {
    if (model_id.empty()) return;
    std::lock_guard<std::mutex> lock(model_metrics_mutex_);
    auto it = model_metrics_.find(model_id);
    if (it != model_metrics_.end() && it->second.in_flight > 0) {
        --it->second.in_flight;
    }
}

RouteDecision SnapLLMServer::choose_scheduled_model(const RouteRequest& request) {
    const auto loaded = manager_->get_loaded_models();
    std::vector<ModelType> types;
    std::vector<ModelLoad> loads;
    types.reserve(loaded.size());
    loads.reserve(loaded.size());
    std::lock_guard<std::mutex> metrics_lock(model_metrics_mutex_);
    for (const auto& name : loaded) {
        types.push_back(manager_->get_model_type(name));
        ModelLoad load;
        const auto it = model_metrics_.find(name);
        if (it != model_metrics_.end()) {
            load.in_flight = it->second.in_flight;
            load.average_latency_ms = it->second.requests > 0
                ? it->second.total_latency_ms / static_cast<double>(it->second.requests) : 0.0;
        }
        loads.push_back(load);
    }
    return RequestRouter::choose(request, loaded, types, manager_->get_current_model(),
                                 loads, routing_cursor_.fetch_add(1, std::memory_order_relaxed));
}

// ============================================================================
// Server Lifecycle
// ============================================================================

bool SnapLLMServer::start() {
    std::cout << "\n";
    std::cout << "================================================================\n";
    std::cout << "  SnapLLM HTTP Server v" << SNAPLLM_VERSION << "\n";
    std::cout << "================================================================\n";
    std::cout << "  Listening on: http://" << config_.host << ":" << config_.port << "\n";
    std::cout << "  Workspace:    " << config_.workspace_root << "\n";
    std::cout << "  CORS:         " << (config_.cors_enabled ? "enabled" : "disabled") << "\n";
    if (!config_.ui_dir.empty()) {
        std::cout << "  Web UI:       http://" << config_.host << ":" << config_.port << "/\n";
        std::cout << "  UI Files:     " << config_.ui_dir << "\n";
    }
    std::cout << "================================================================\n";
    std::cout << "\n";
    std::cout << "  API Endpoints:\n";
    std::cout << "    GET  /health                      - Health check\n";
    std::cout << "    GET  /v1/models                   - List models (OpenAI)\n";
    std::cout << "    POST /v1/chat/completions         - Chat completion (OpenAI)\n";
    std::cout << "    POST /v1/messages                 - Messages API (Anthropic/Claude Code)\n";
    std::cout << "    GET  /api/v1/models               - List models (extended)\n";
    std::cout << "    POST /api/v1/models/load          - Load model\n";
    std::cout << "    POST /api/v1/models/switch        - Select a loaded model\n";
    std::cout << "    POST /api/v1/models/unload        - Unload model\n";
    std::cout << "    GET  /api/v1/models/cache/stats   - Cache statistics\n";
    std::cout << "    GET  /ws/stream                   - Unsupported; use SSE streaming\n";
    std::cout << "    POST /api/v1/models/cache/clear   - Unsupported in this runtime\n";
    std::cout << "    POST /api/v1/generate             - Text generation\n";
    std::cout << "    POST /api/v1/generate/batch       - Batch generation\n";
#ifdef SNAPLLM_HAS_DIFFUSION
    std::cout << "    POST /api/v1/diffusion/generate   - Image generation\n";
    std::cout << "    POST /api/v1/diffusion/video      - Unsupported in this runtime\n";
#endif
#ifdef SNAPLLM_HAS_MULTIMODAL
    std::cout << "    POST /api/v1/vision/generate      - Vision/multimodal\n";
#endif
    std::cout << "\n";
    std::cout << "  Context API (vPID L2 - KV Cache Persistence):\n";
    std::cout << "    POST /api/v1/contexts/ingest      - Ingest context (pre-compute KV)\n";
    std::cout << "    GET  /api/v1/contexts             - List contexts\n";
    std::cout << "    GET  /api/v1/contexts/:id         - Get context info\n";
    std::cout << "    POST /api/v1/contexts/:id/query   - Query with cached context\n";
    std::cout << "    DELETE /api/v1/contexts/:id       - Delete context\n";
    std::cout << "    POST /api/v1/contexts/:id/promote - Promote to hot tier\n";
    std::cout << "    POST /api/v1/contexts/:id/demote  - Demote to cold tier\n";
    std::cout << "    GET  /api/v1/contexts/stats       - Context statistics\n";
    std::cout << "\n";
    std::cout << "  Press Ctrl+C to stop the server.\n";
    std::cout << "================================================================\n\n";

    // Mount Web UI static files directory if configured
    if (!config_.ui_dir.empty()) {
        if (svr_->set_mount_point("/", config_.ui_dir)) {
            std::cout << "[Server] Serving Web UI from: " << config_.ui_dir << std::endl;
        } else {
            std::cerr << "[Server] Warning: Failed to mount UI directory: " << config_.ui_dir << std::endl;
        }
    }

    // Auto-open browser for Web UI after server starts
    if (!config_.ui_dir.empty()) {
        std::string url = "http://" + config_.host + ":" + std::to_string(config_.port) + "/";
        std::thread([url]() {
            // Brief delay to let the server socket bind
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            if (open_url_without_shell(url)) {
                std::cout << "[Server] Opened Web UI in browser: " << url << std::endl;
            } else {
                std::cerr << "[Server] Could not open Web UI automatically; visit: "
                          << url << std::endl;
            }
        }).detach();
    }

    running_ = true;
    bool result = svr_->listen(config_.host.c_str(), config_.port);
    running_ = false;

    if (!result) {
        std::cerr << "[SnapLLM Server] Failed to start server on "
                  << config_.host << ":" << config_.port << std::endl;
    }

    return result;
}

void SnapLLMServer::stop() {
    if (running_) {
        std::cout << "\n[SnapLLM Server] Shutting down...\n";
        svr_->stop();
        running_ = false;
    }
}

bool SnapLLMServer::is_running() const {
    return running_;
}

std::shared_ptr<ModelManager> SnapLLMServer::get_model_manager() {
    return manager_;
}

// ============================================================================
// Middleware Setup
// ============================================================================

void SnapLLMServer::setup_middleware() {
    // Exception handler
    svr_->set_exception_handler([this](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            std::cerr << "[SnapLLM Server] Unhandled request exception: "
                      << e.what() << std::endl;
            send_error(res, "Internal server error", "server_error", 500);
        } catch (...) {
            send_error(res, "Unknown server error", "server_error", 500);
        }
    });
}

bool SnapLLMServer::authorize_request(
    const httplib::Request& req,
    httplib::Response& res,
    bool require_authentication
) {
    const std::string host = req.get_header_value("Host");
    if (!is_safe_host_header(host) ||
        (security::is_loopback_host(config_.host) &&
         !security::is_valid_loopback_host_header(
             host, static_cast<std::uint16_t>(config_.port)))) {
        send_error(res, "Invalid Host header", "invalid_request_error", 400);
        return false;
    }

    const std::string origin = req.get_header_value("Origin");
    if (!origin.empty()) {
        if (!security::is_browser_origin_allowed(origin, config_.allowed_origins)) {
            send_error(res, "Browser origin is not allowed", "forbidden", 403);
            return false;
        }
        res.set_header("Access-Control-Allow-Origin", origin);
        res.set_header("Vary", "Origin");
    }

    if (require_authentication && !config_.api_key.empty()) {
        const bool authorized =
            security::bearer_api_key_matches(
                req.get_header_value("Authorization"), config_.api_key) ||
            security::x_api_key_matches(
                req.get_header_value("X-API-Key"), config_.api_key);
        if (!authorized) {
            res.set_header("WWW-Authenticate", "Bearer");
            send_error(res, "Authentication required", "authentication_error", 401);
            return false;
        }
    }
    return true;
}

// ============================================================================
// Route Setup
// ============================================================================

bool SnapLLMServer::dispatch_post(const httplib::Request& req, httplib::Response& res) {
    if (!authorize_request(req, res, true)) {
        return true;
    }
    const std::string& path = req.path;

    if (path == "/api/v1/config") {
        handle_config_update(req, res);
        return true;
    }

    // Model management POST routes
    if (path == "/api/v1/models/load") {
        handle_load_model(req, res);
        return true;
    }
    if (path == "/api/v1/models/switch") {
        handle_switch_model(req, res);
        return true;
    }
    if (path == "/api/v1/models/unload") {
        handle_unload_model(req, res);
        return true;
    }
    if (path == "/api/v1/models/scan" || path == "/models/scan") {
        handle_scan_folder(req, res);
        return true;
    }
    if (path == "/api/v1/models/cache/clear") {
        handle_cache_clear(req, res);
        return true;
    }

    // Chat and messages POST routes
    if (path == "/v1/chat/completions") {
        handle_chat_completions(req, res);
        return true;
    }
    if (path == "/v1/messages") {
        handle_messages(req, res);
        return true;
    }

    // Generation POST routes
    if (path == "/api/v1/generate") {
        handle_generate(req, res);
        return true;
    }
    if (path == "/api/v1/generate/batch") {
        handle_generate_batch(req, res);
        return true;
    }

    // Context POST routes
    if (path == "/api/v1/contexts/ingest") {
        handle_context_ingest(req, res);
        return true;
    }
    // Context routes with ID patterns
    if (path.rfind("/api/v1/contexts/", 0) == 0) {
        // Extract context_id from paths like /api/v1/contexts/{id}/query
        std::string suffix = path.substr(17); // Remove "/api/v1/contexts/"
        size_t slash_pos = suffix.find('/');
        if (slash_pos != std::string::npos) {
            std::string context_id = suffix.substr(0, slash_pos);
            std::string action = suffix.substr(slash_pos + 1);
            if (action == "query") {
                handle_context_query(req, res, context_id);
                return true;
            }
            if (action == "promote") {
                handle_context_promote(req, res, context_id);
                return true;
            }
            if (action == "demote") {
                handle_context_demote(req, res, context_id);
                return true;
            }
        }
        return false;
    }

    // Diffusion POST routes
    if (path == "/api/v1/diffusion/generate") {
        handle_diffusion_generate(req, res);
        return true;
    }
    if (path == "/api/v1/diffusion/video") {
        handle_diffusion_video(req, res);
        return true;
    }

    // Vision POST routes
    if (path == "/api/v1/vision/generate") {
        handle_vision_generate(req, res);
        return true;
    }

    return false;
}

void SnapLLMServer::setup_routes() {
    std::cout << "[Server] Registering routes..." << std::endl;

    // =========================================================================
    // WORKAROUND: Use catch-all POST handler due to httplib routing bug
    // When many routes are registered, individual POST routes return 404.
    // Using a single regex catch-all that manually routes works.
    // =========================================================================

    svr_->Post(R"(/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!dispatch_post(req, res)) {
            send_error(res, "Not found: " + req.path, "not_found", 404);
        }
    });
    svr_->Options(R"(/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, false)) return;
        if (req.get_header_value("Origin").empty()) {
            send_error(res, "CORS preflight requires an allowed Origin", "forbidden", 403);
            return;
        }
        res.status = 204;
        res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.set_header(
            "Access-Control-Allow-Headers",
            "Content-Type, Authorization, X-API-Key");
        res.set_header("Access-Control-Max-Age", "600");
    });

    std::cout << "[Server] Registered catch-all POST handler" << std::endl;

    // =========================================================================
    // ALL GET ROUTES
    // =========================================================================

    // API info endpoint (always available)
    svr_->Get("/api", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        json response = {
            {"name", "SnapLLM API"},
            {"version", SNAPLLM_VERSION},
            {"status", "running"},
            {"description", "Local multi-model LLM inference with resident model selection"},
            {"endpoints", {
                {"health", "/health"},
                {"models", "/api/v1/models"},
                {"load_model", "/api/v1/models/load"},
                {"switch_model", "/api/v1/models/switch"},
                {"chat", "/v1/chat/completions"},
                {"generate", "/api/v1/generate"},
                {"vision", "/api/v1/vision/generate"},
                {"diffusion", "/api/v1/diffusion/generate"}
            }}
        };
        send_json(res, response.dump());
    });

    // Root path - serve Web UI index.html if available, otherwise API info
    svr_->Get("/", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, false)) return;
        if (!config_.ui_dir.empty()) {
            auto index_path = fs::path(config_.ui_dir) / "index.html";
            if (fs::exists(index_path)) {
                std::ifstream file(index_path, std::ios::binary);
                if (file) {
                    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    res.set_content(content, "text/html");
                    return;
                }
            }
        }
        // Fallback: JSON API info
        json response = {
            {"name", "SnapLLM API"},
            {"version", SNAPLLM_VERSION},
            {"status", "running"},
            {"description", "Local multi-model LLM inference with resident model selection"},
            {"ui", config_.ui_dir.empty() ? "not configured" : "enabled at /"},
            {"endpoints", {
                {"health", "/health"},
                {"models", "/api/v1/models"},
                {"load_model", "/api/v1/models/load"},
                {"switch_model", "/api/v1/models/switch"},
                {"chat", "/v1/chat/completions"},
                {"generate", "/api/v1/generate"},
                {"vision", "/api/v1/vision/generate"},
                {"diffusion", "/api/v1/diffusion/generate"}
            }}
        };
        send_json(res, response.dump());
    });
    svr_->Get("/health", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, false)) return;
        handle_health(req, res);
    });
    svr_->Get("/v1/health", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, false)) return;
        handle_health(req, res);
    });

    // Config and recommendations endpoints for Settings page
    svr_->Get("/api/v1/config", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        int active_inferences = 0;
        int max_active_inferences = 0;
        {
            std::lock_guard<std::mutex> lock(inference_gate_mutex_);
            active_inferences = active_inference_count_;
            max_active_inferences = max_active_inferences_;
        }
        json response = {
            {"status", "success"},
            {"max_models", config_.max_models},
            {"default_ram_budget_mb", config_.default_ram_budget_mb},
            {"default_strategy", config_.default_strategy},
            {"enable_gpu", config_.enable_gpu},
            {"workspace_root", config_.workspace_root},
            {"default_models_path", config_.default_models_path.empty() ? get_default_models_path() : config_.default_models_path},
            {"config_path", config_.config_path},
            {"port", config_.port},
            {"host", config_.host},
            {"cors_enabled", config_.cors_enabled},
            {"timeout_seconds", config_.timeout_seconds},
            {"max_concurrent_requests", config_.max_concurrent_requests},
            {"max_active_inferences", max_active_inferences},
            {"scheduler", {
                {"max_active_inferences", max_active_inferences},
                {"active_inferences", active_inferences},
                {"waiting_inferences", waiting_inference_count_.load(std::memory_order_relaxed)},
                {"queue_limit", std::min(config_.max_concurrent_requests * 8, 256)}
            }},
            {"features", {
                {"llm", true},
#ifdef SNAPLLM_HAS_DIFFUSION
                {"diffusion", true},
#else
                {"diffusion", false},
#endif
#ifdef SNAPLLM_HAS_MULTIMODAL
                {"vision", true},
#else
                {"vision", false},
#endif
                {"video", false}
            }}
        };
        send_json(res, response.dump());
    });
    svr_->Get("/api/v1/config/recommendations", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        // Get system memory info
        size_t total_ram_mb = 32768;  // Default 32GB
#ifdef _WIN32
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            total_ram_mb = memInfo.ullTotalPhys / (1024 * 1024);
        }
#endif
        size_t recommended_budget = (total_ram_mb * 70) / 100;  // 70% of RAM
        int max_concurrent = static_cast<int>(total_ram_mb / 8192);  // 8GB per model estimate
        if (max_concurrent < 1) max_concurrent = 1;
        if (max_concurrent > 10) max_concurrent = 10;

        json response = {
            {"status", "success"},
            {"recommended_ram_budget_mb", recommended_budget},
            {"recommended_strategy", total_ram_mb > 32768 ? "performance" : "balanced"},
            {"total_ram_gb", total_ram_mb / 1024.0},
            {"max_concurrent_models", max_concurrent}
        };
        send_json(res, response.dump());
    });
    // Server metrics endpoint for Dashboard
    svr_->Get("/api/v1/server/metrics", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        handle_server_metrics(req, res);
    });
    svr_->Get("/v1/models", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        handle_models_openai(req, res);
    });
    svr_->Get("/api/v1/models", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        handle_models_extended(req, res);
    });
    svr_->Get("/api/v1/models/", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        handle_models_extended(req, res);
    });
    svr_->Get("/api/v1/models/cache/stats", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        handle_cache_stats(req, res);
    });
    svr_->Get("/api/v1/contexts", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        handle_context_list(req, res);
    });
    svr_->Get("/api/v1/contexts/stats", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        handle_context_stats(req, res);
    });
    svr_->Get(R"(/api/v1/contexts/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        std::string context_id = req.matches[1];
        handle_context_get(req, res, context_id);
    });
    svr_->Get("/ws/stream", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        handle_websocket_upgrade(req, res);
    });

#ifdef SNAPLLM_HAS_DIFFUSION
    svr_->Get(R"(/api/v1/images/([^/]+\.png))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        std::string filename = req.matches[1];
        const fs::path images_dir = fs::path(config_.workspace_root) / "images";
        auto filepath = security::canonical_path_within_root(
            images_dir, images_dir / filename);
        if (!filepath || !fs::exists(*filepath)) {
            send_error(res, "Image not found: " + filename, "not_found", 404);
            return;
        }
        const auto file_size = limits::bounded_regular_file_size(*filepath);
        if (!file_size) {
            send_error(res, "Image is not a bounded regular file", "invalid_asset", 413);
            return;
        }
        std::ifstream file(*filepath, std::ios::binary);
        if (!file) {
            send_error(res, "Failed to read image", "server_error", 500);
            return;
        }
        std::vector<char> buffer(static_cast<std::size_t>(*file_size));
        if (!buffer.empty() &&
            !file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()))) {
            send_error(res, "Failed to read image", "server_error", 500);
            return;
        }
        res.set_content(buffer.data(), buffer.size(), "image/png");
    });
    svr_->Get(R"(/api/v1/videos/([^/]+)/([^/]+\.png))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        std::string video_id = req.matches[1];
        std::string filename = req.matches[2];
        const fs::path videos_dir = fs::path(config_.workspace_root) / "videos";
        auto filepath = security::canonical_path_within_root(
            videos_dir, videos_dir / video_id / filename);
        if (!filepath || !fs::exists(*filepath)) {
            send_error(res, "Frame not found: " + filename, "not_found", 404);
            return;
        }
        const auto file_size = limits::bounded_regular_file_size(*filepath);
        if (!file_size) {
            send_error(res, "Frame is not a bounded regular file", "invalid_asset", 413);
            return;
        }
        std::ifstream file(*filepath, std::ios::binary);
        if (!file) {
            send_error(res, "Failed to read frame", "server_error", 500);
            return;
        }
        std::vector<char> buffer(static_cast<std::size_t>(*file_size));
        if (!buffer.empty() &&
            !file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()))) {
            send_error(res, "Failed to read frame", "server_error", 500);
            return;
        }
        res.set_content(buffer.data(), buffer.size(), "image/png");
    });
#endif

    std::cout << "[Server] Registered all GET routes" << std::endl;

    // =========================================================================
    // DELETE ROUTES
    // =========================================================================

    svr_->Delete(R"(/api/v1/models/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        const auto decoded_name = decode_model_path_component(req.matches[1].str());
        if (!decoded_name) {
            send_error(res, "Invalid model identifier", "invalid_request_error", 400);
            return;
        }
        std::string name = *decoded_name;
        std::cout << "[Server] DELETE model request (" << name.size()
                  << " identifier bytes)" << std::endl;

        bool unloaded = false;
        std::string model_type = "llm";

#ifdef SNAPLLM_HAS_DIFFUSION
        // Check if this is a diffusion model first
        auto* diffusion_bridge = get_diffusion_bridge(config_.workspace_root);
        auto loaded_diffusion = diffusion_bridge->get_loaded_models();
        std::cout << "[Server] Loaded diffusion models: ";
        for (const auto& m : loaded_diffusion) std::cout << "'" << m << "' ";
        std::cout << std::endl;

        if (diffusion_bridge->is_model_loaded(name)) {
            std::cout << "[Server] Found in diffusion bridge, unloading..." << std::endl;
            diffusion_bridge->unload_model(name);
            unloaded = true;
            model_type = "diffusion";
            std::cout << "[Server] Unloaded diffusion model: " << name << std::endl;
        } else {
            std::cout << "[Server] Not found in diffusion bridge" << std::endl;
        }
#endif

        // If not a diffusion model, try unloading from LLM manager
        if (!unloaded) {
            std::cout << "[Server] Checking LLM manager for: " << name << std::endl;
            if (manager_->is_loaded(name)) {
                manager_->unload_model(name);
                unloaded = true;
                std::cout << "[Server] Unloaded LLM model: " << name << std::endl;
            } else {
                std::cout << "[Server] Not found in LLM manager either" << std::endl;
            }
        }

        if (unloaded) {
            json response = {
                {"status", "success"},
                {"message", "Model unloaded: " + name},
                {"model_type", model_type},
                {"current_model", manager_->get_current_model()}
            };
            send_json(res, response.dump());
        } else {
            send_error(res, "Model not found: " + name, "not_found", 404);
        }
    });
    svr_->Delete(R"(/api/v1/contexts/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize_request(req, res, true)) return;
        std::string context_id = req.matches[1];
        handle_context_delete(req, res, context_id);
    });

    // SPA fallback is a normal final GET route. Keeping it out of the global
    // error handler prevents authentication and Origin failures from being
    // rewritten or accidentally dispatched a second time.
    svr_->Get(R"(/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
        const bool is_api_path =
            req.path.rfind("/api/", 0) == 0 ||
            req.path.rfind("/v1/", 0) == 0 ||
            req.path.rfind("/ws/", 0) == 0;
        if (!authorize_request(req, res, is_api_path)) return;
        if (!config_.ui_dir.empty() && !is_api_path) {
            const auto index_path = fs::path(config_.ui_dir) / "index.html";
            std::ifstream file(index_path, std::ios::binary);
            if (file) {
                std::string content(
                    (std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());
                res.set_content(content, "text/html");
                return;
            }
        }
        send_error(res, "Not found: " + req.path, "not_found", 404);
    });

    std::cout << "[Server] Route registration complete" << std::endl;
}

// ============================================================================
// Health Endpoint
// ============================================================================

void SnapLLMServer::handle_health(const httplib::Request&, httplib::Response& res) {
    // Get ISO timestamp string
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%SZ");
    std::string timestamp_iso = ss.str();

    json response = {
        {"status", "ok"},
        {"version", SNAPLLM_VERSION},
        {"timestamp", timestamp_iso}
    };

    send_json(res, response.dump());
}

// ============================================================================
// Models Endpoints
// ============================================================================

void SnapLLMServer::handle_models_openai(const httplib::Request&, httplib::Response& res) {
    auto models = manager_->get_loaded_models();
    std::string current = manager_->get_current_model();
    int64_t timestamp = get_timestamp();

    json data = json::array();
    for (const auto& model : models) {
        data.push_back({
            {"id", model},
            {"object", "model"},
            {"created", timestamp},
            {"owned_by", "snapllm"}
        });
    }

    json response = {
        {"object", "list"},
        {"data", data}
    };

    send_json(res, response.dump());
}

void SnapLLMServer::handle_models_extended(const httplib::Request& req, httplib::Response& res) {
    // Get type filter from query params (?type=llm, ?type=diffusion, ?type=vision, etc.)
    std::string type_filter = "";
    if (req.has_param("type")) {
        type_filter = req.get_param_value("type");
    }

    auto models = manager_->get_loaded_models();
    std::string current = manager_->get_current_model();

    json models_array = json::array();

    // Add LLM models (from ModelManager)
    for (const auto& model : models) {
        std::string model_type = "llm";  // Default type for models in ModelManager
        // `memory_bytes` is the bridge's tracked GPU residency for the model.
        // CPU-only loads intentionally report zero here, so do not label them
        // as GPU-backed merely because the server was built with GPU support.
        const auto model_info = manager_->get_model_info(model);
        const std::string device =
            model_info && model_info->memory_bytes > 0 ? "gpu" : "cpu";

        // Check if this model should be included based on filter
        if (!type_filter.empty() && type_filter != "llm" && type_filter != "text") {
            continue;  // Skip LLM models if filtering for other types
        }

        models_array.push_back({
            {"id", model},
            {"name", model},
            {"type", model_type},
            {"active", model == current},
            {"status", "loaded"},
            {"engine", "vpid"},
            {"device", device}
        });
    }

#ifdef SNAPLLM_HAS_DIFFUSION
    // Add diffusion models (from shared DiffusionBridge)
    auto* diffusion_bridge_ptr = get_diffusion_bridge(config_.workspace_root);
    auto diffusion_models = diffusion_bridge_ptr->get_loaded_models();
    for (const auto& model : diffusion_models) {
        // Check if this model should be included based on filter
        if (!type_filter.empty() && type_filter != "diffusion" && type_filter != "sd" && type_filter != "image") {
            continue;  // Skip diffusion models if filtering for other types
        }

        models_array.push_back({
            {"id", model},
            {"name", model},
            {"type", "diffusion"},
            {"active", false},
            {"status", "loaded"},
            {"engine", "stable-diffusion"},
            {"device", "gpu"}
        });
    }
#endif

#ifdef SNAPLLM_HAS_MULTIMODAL
    // Add vision models (from shared MultimodalBridge)
    auto* multimodal_bridge_ptr = get_multimodal_bridge();
    if (multimodal_bridge_ptr->is_loaded()) {
        // Check if this model should be included based on filter
        if (type_filter.empty() || type_filter == "vision" || type_filter == "vl" || type_filter == "multimodal") {
            std::string vision_model_name = multimodal_bridge_ptr->get_model_info();
            models_array.push_back({
                {"id", vision_model_name},
                {"name", vision_model_name},
                {"type", "vision"},
                {"active", true},
                {"status", "loaded"},
                {"engine", "mtmd"},
                {"device", "gpu"}
            });
        }
    }
#endif

    json response = {
        {"status", "success"},
        {"models", models_array},
        {"count", static_cast<int>(models_array.size())},
        {"current_model", current.empty() ? nullptr : json(current)}
    };

    send_json(res, response.dump());
}

// ============================================================================
// Chat Completions Endpoint (OpenAI-compatible)
// With MCB (Model Context Bucket) integration for automatic L2 Context caching
// ============================================================================

void SnapLLMServer::handle_chat_completions(const httplib::Request& req, httplib::Response& res) {
    total_requests_++;

    // === INFERENCE GATE: Acquire slot before ANY model/GPU operations ===
    if (!acquire_inference_gate(config_.timeout_seconds * 1000)) {
        total_errors_++;
        send_error(res, "Server busy - too many concurrent inference requests. Please retry.",
                   "server_busy", 503);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);
        if (!body.is_object()) {
            send_error(res, "Request body must be a JSON object");
            return;
        }

        // Extract parameters
        std::string validation_error;
        std::string model;
        if (!read_bounded_string(
                body, "model", "",
                limits::kMaximumStringBytes, true, model, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        RouteRequest route_request;
        route_request.requested_model = model;
        if (body.contains("routing") && body["routing"].is_object()) {
            if (!read_bounded_string(body["routing"], "task", "", limits::kMaximumStringBytes,
                                     true, route_request.task, validation_error) ||
                !read_bounded_string(body["routing"], "modality", "text", limits::kMaximumStringBytes,
                                     true, route_request.modality, validation_error)) {
                send_error(res, validation_error);
                return;
            }
        }
        auto loaded_for_route = manager_->get_loaded_models();
        std::vector<ModelType> types_for_route;
        types_for_route.reserve(loaded_for_route.size());
        for (const auto& loaded_name : loaded_for_route) {
            types_for_route.push_back(manager_->get_model_type(loaded_name));
        }
        std::vector<ModelLoad> loads_for_route;
        loads_for_route.reserve(loaded_for_route.size());
        {
            std::lock_guard<std::mutex> metrics_lock(model_metrics_mutex_);
            for (const auto& loaded_name : loaded_for_route) {
                ModelLoad load;
                const auto it = model_metrics_.find(loaded_name);
                if (it != model_metrics_.end()) {
                    load.in_flight = it->second.in_flight;
                    load.average_latency_ms = it->second.requests > 0
                        ? it->second.total_latency_ms / static_cast<double>(it->second.requests)
                        : 0.0;
                }
                loads_for_route.push_back(load);
            }
        }
        const auto route = RequestRouter::choose(route_request, loaded_for_route,
                                                 types_for_route, manager_->get_current_model(),
                                                 loads_for_route,
                                                 routing_cursor_.fetch_add(1, std::memory_order_relaxed));
        if (!route.accepted) {
            send_error(res, route.error, "route_rejected", 422);
            return;
        }
        model = route.model;
        // Streaming is the responsive default for chat clients. Callers that
        // require a single buffered JSON response can still opt out with
        // `"stream": false`.
        bool stream = body.value("stream", true);
        int max_tokens = 0;
        if (!read_bounded_integer(
                body, "max_tokens", 2000,
                limits::kMinimumMaxTokens, limits::kMaximumMaxTokens,
                max_tokens, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        float temperature = 0.0f;
        float top_p = 0.0f;
        float repeat_penalty = 0.0f;
        int top_k = 0;
        if (!read_bounded_float(body, "temperature", 0.8, 0.0, 2.0, temperature, validation_error) ||
            !read_bounded_float(body, "top_p", 0.95, 0.0, 1.0, top_p, validation_error) ||
            !read_bounded_integer(body, "top_k", 40, 0, 1000, top_k, validation_error) ||
            !read_bounded_float(body, "repeat_penalty", 1.1, 0.0, 10.0, repeat_penalty, validation_error)) {
            send_error(res, validation_error);
            return;
        }

        // MCB: Option to enable L2 Context caching
        // Memory leak in KVCacheExtractor fixed - contexts now cached per model
        bool use_context_cache = body.value("use_context_cache", true);

        if (!body.contains("messages")) {
            send_error(res, "Missing 'messages' array in request body");
            return;
        }
        const auto& messages = body["messages"];
        std::size_t total_message_bytes = 0;
        if (!validate_text_messages(
                messages, validation_error, total_message_bytes)) {
            send_error(res, validation_error);
            return;
        }

        // Resolve the request model without mutating process-wide selection.
        std::string current_model = model.empty() ? manager_->get_current_model() : model;
        model_request_started(current_model);
        InferenceGateGuard model_metrics_guard(true, [this, current_model]() {
            model_request_finished(current_model);
        });
        if (current_model.empty()) {
            send_error(res, "No model loaded. Load a model first via POST /api/v1/models/load", "no_model", 400);
            return;
        }
        if (!manager_->is_loaded(current_model)) {
            send_error(res, "Model not loaded: " + current_model, "model_not_found", 404);
            return;
        }

        std::string completion_id = generate_completion_id();
        int64_t created = get_timestamp();

        // MCB: Separate context (history) from query (last user message)
        // Context = all messages except the last user message
        // Query = the last user message only
        std::string context_text;
        std::string query_text;
        std::vector<std::string> message_roles;
        message_roles.reserve(messages.size());
        for (const auto& message : messages) {
            message_roles.push_back(message.value("role", ""));
        }
        std::vector<std::string_view> role_views;
        role_views.reserve(message_roles.size());
        for (const auto& role : message_roles) {
            role_views.push_back(role);
        }
        const auto query_index =
            limits::find_last_role_index(role_views, "user");
        if (query_index) {
            query_text = messages[*query_index].value("content", "");
        }

        // Build context from all messages except the last user message
        for (std::size_t message_index = 0;
             message_index < messages.size();
             ++message_index) {
            const auto& msg = messages[message_index];
            std::string role = msg.value("role", "user");
            std::string content = msg.value("content", "");

            // Skip the last user message (that's our query)
            if (query_index && message_index == *query_index) {
                continue;
            }

            if (role == "system") {
                context_text += "System: " + content + "\n\n";
            } else if (role == "user") {
                context_text += "User: " + content + "\n\n";
            } else if (role == "assistant") {
                context_text += "Assistant: " + content + "\n\n";
            }
        }

        // MCB: Check if we should use L2 Context caching
        // Only use context caching when there's actual conversation history to cache
        // (not for first message which has no prior context)
        bool using_cached_context = false;
        ContextHandle context_handle;

        if (use_context_cache && context_manager_ && !context_text.empty()) {
            // MCB: Try to use context caching only when we have actual context
            // Try to find or create a cached context for the conversation history
            context_handle = context_manager_->find_or_create(
                current_model,
                context_text,
                "chat_session"
            );

            if (context_handle.valid) {
                using_cached_context = true;
                std::cout << "[SnapLLM MCB] Using cached context for chat ("
                          << query_text.size() << " query bytes)" << std::endl;
            }
        }

        if (stream) {
            // SSE Streaming response
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");

            if (using_cached_context) {
                // MCB: Use cached context with streaming
                ContextQueryConfig config;
                config.max_tokens = static_cast<uint32_t>(max_tokens);
                config.temperature = temperature;
                config.top_p = top_p;
                config.top_k = top_k;
                config.repeat_penalty = repeat_penalty;
                config.stream = true;

                res.set_chunked_content_provider(
                    MIMETYPE_SSE,
                    [this, context_handle, query_text, config,
                     completion_id, created, current_model](size_t /*offset*/, httplib::DataSink& sink) {

                        auto stream_start = std::chrono::high_resolution_clock::now();
                        size_t streamed_tokens = context_manager_->query_streaming(
                            context_handle,
                            "User: " + query_text + "\n\nAssistant:",
                            [&sink, &completion_id, &created, &current_model](
                                const std::string& token, int /*token_id*/, bool is_done) {

                                if (!sink.is_writable()) {
                                    return false;  // Client disconnected
                                }

                                // Build chunk
                                json chunk = {
                                    {"id", completion_id},
                                    {"object", "chat.completion.chunk"},
                                    {"created", created},
                                    {"model", current_model},
                                    {"choices", json::array({
                                        {
                                            {"index", 0},
                                            {"delta", {{"content", token}}},
                                            {"finish_reason", nullptr}
                                        }
                                    })},
                                    {"x_mcb_cache_hit", true}  // MCB indicator
                                };

                                std::string data = "data: " + chunk.dump() + "\n\n";
                                sink.write(data.data(), data.size());
                                return sink.is_writable();
                            },
                            config
                        );
                        auto stream_end = std::chrono::high_resolution_clock::now();
                        double latency_ms = std::chrono::duration<double, std::milli>(stream_end - stream_start).count();

                        total_tokens_ += streamed_tokens;
                        record_model_metrics(current_model, streamed_tokens, latency_ms);

                        // Send final chunk with finish_reason
                        json final_chunk = {
                            {"id", completion_id},
                            {"object", "chat.completion.chunk"},
                            {"created", created},
                            {"model", current_model},
                            {"choices", json::array({
                                {
                                    {"index", 0},
                                    {"delta", json::object()},
                                    {"finish_reason", "stop"}
                                }
                            })}
                        };

                        std::string final_data = "data: " + final_chunk.dump() + "\n\n";
                        sink.write(final_data.data(), final_data.size());

                        // Send [DONE] terminator
                        std::string done = "data: [DONE]\n\n";
                        sink.write(done.data(), done.size());

                        sink.done();
                        return false;  // Done
                    }
                );

            } else {
                // Fallback: No cached context, use direct generation
                std::string full_prompt = context_text + "User: " + query_text + "\n\nAssistant:";

                res.set_chunked_content_provider(
                    MIMETYPE_SSE,
                    [this, full_prompt, max_tokens, temperature, top_p, top_k, repeat_penalty,
                     completion_id, created, current_model](size_t /*offset*/, httplib::DataSink& sink) {
                        auto stream_start = std::chrono::high_resolution_clock::now();
                        size_t streamed_tokens = manager_->generate_streaming_for_model(
                            current_model, full_prompt,
                            [&sink, &completion_id, &created, &current_model](
                                const std::string& token, int /*token_id*/, bool is_eos) -> bool {

                                if (!sink.is_writable()) {
                                    return false;
                                }

                                json chunk = {
                                    {"id", completion_id},
                                    {"object", "chat.completion.chunk"},
                                    {"created", created},
                                    {"model", current_model},
                                    {"choices", json::array({
                                        {
                                            {"index", 0},
                                            {"delta", {{"content", token}}},
                                            {"finish_reason", nullptr}
                                        }
                                    })}
                                };

                                std::string data = "data: " + chunk.dump() + "\n\n";
                                sink.write(data.data(), data.size());

                                return !is_eos;
                            },
                            static_cast<size_t>(max_tokens),
                            temperature, top_p, top_k, repeat_penalty
                        );
                        auto stream_end = std::chrono::high_resolution_clock::now();
                        double latency_ms = std::chrono::duration<double, std::milli>(stream_end - stream_start).count();

                        total_tokens_ += streamed_tokens;
                        record_model_metrics(current_model, streamed_tokens, latency_ms);

                        json final_chunk = {
                            {"id", completion_id},
                            {"object", "chat.completion.chunk"},
                            {"created", created},
                            {"model", current_model},
                            {"choices", json::array({
                                {
                                    {"index", 0},
                                    {"delta", json::object()},
                                    {"finish_reason", "stop"}
                                }
                            })}
                        };

                        std::string final_data = "data: " + final_chunk.dump() + "\n\n";
                        sink.write(final_data.data(), final_data.size());

                        std::string done = "data: [DONE]\n\n";
                        sink.write(done.data(), done.size());

                        sink.done();
                        return false;
                    }
                );
            }

        } else {
            // Non-streaming response
            auto start_time = std::chrono::high_resolution_clock::now();

            std::string result;
            int prompt_tokens = 0;
            int completion_tokens = 0;
            bool cache_hit = false;

            if (using_cached_context) {
                // MCB: Use cached context
                ContextQueryConfig config;
                config.max_tokens = static_cast<uint32_t>(max_tokens);
                config.temperature = temperature;
                config.top_p = top_p;
                config.top_k = top_k;
                config.repeat_penalty = repeat_penalty;

                ContextQueryResult query_result = context_manager_->query(
                    context_handle,
                    "User: " + query_text + "\n\nAssistant:",
                    config
                );

                result = query_result.text;
                prompt_tokens = static_cast<int>(query_result.usage.context_tokens + query_result.usage.query_tokens);
                completion_tokens = static_cast<int>(query_result.usage.generated_tokens);
                cache_hit = query_result.cache_hit;

            } else {
                // Fallback: Direct generation
                std::string full_prompt = context_text + "User: " + query_text + "\n\nAssistant:";

                size_t actual_tokens = 0;
                result = manager_->generate_for_model(
                    current_model, full_prompt, static_cast<size_t>(max_tokens), &actual_tokens,
                    temperature, top_p, top_k, repeat_penalty
                );

                prompt_tokens = estimate_tokens(full_prompt);
                completion_tokens = static_cast<int>(actual_tokens);
                if (completion_tokens == 0) {
                    completion_tokens = estimate_tokens(result);
                }
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            double generation_time = std::chrono::duration<double>(end_time - start_time).count();
            double latency_ms = generation_time * 1000.0;  // Convert to milliseconds for frontend
            double tokens_per_second = (generation_time > 0) ? (completion_tokens / generation_time) : 0;

            // Update metrics
            total_tokens_ += completion_tokens;
            record_model_metrics(current_model, static_cast<uint64_t>(completion_tokens), latency_ms);

            json response = {
                {"id", completion_id},
                {"object", "chat.completion"},
                {"created", created},
                {"model", current_model},
                {"choices", json::array({
                    {
                        {"index", 0},
                        {"message", {
                            {"role", "assistant"},
                            {"content", result}
                        }},
                        {"finish_reason", "stop"}
                    }
                })},
                {"usage", {
                    {"prompt_tokens", prompt_tokens},
                    {"completion_tokens", completion_tokens},
                    {"total_tokens", prompt_tokens + completion_tokens},
                    {"tokens_per_second", tokens_per_second},
                    {"latency_ms", latency_ms},
                    {"context_tokens", using_cached_context ? prompt_tokens : 0}
                }},
                // vPID L2 context cache indicators (at root level for frontend)
                {"cache_hit", cache_hit},
                {"speedup", cache_hit ? "indexed cache lookup" : "uncached"},
                // Legacy x_mcb for backwards compatibility
                {"x_mcb", {
                    {"cache_hit", cache_hit},
                    {"context_id", using_cached_context ? context_handle.id : ""}
                }}
            };

            send_json(res, response.dump());
        }

    } catch (const json::exception&) {
        send_error(res, "Invalid JSON request body");
    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

// ============================================================================
// Anthropic Messages API Endpoint (Claude Code Compatible)
// ============================================================================

void SnapLLMServer::handle_messages(const httplib::Request& req, httplib::Response& res) {
    total_requests_++;

    // === INFERENCE GATE: Acquire slot before ANY model/GPU operations ===
    if (!acquire_inference_gate(config_.timeout_seconds * 1000)) {
        total_errors_++;
        json error = {
            {"type", "error"},
            {"error", {
                {"type", "overloaded_error"},
                {"message", "Server busy - too many concurrent inference requests. Please retry."}
            }}
        };
        res.status = 529;  // Anthropic overloaded status
        res.set_content(error.dump(), MIMETYPE_JSON);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);
        if (!body.is_object()) {
            send_error(res, "Request body must be a JSON object");
            return;
        }

        // Extract parameters (Anthropic format)
        std::string validation_error;
        std::string model;
        if (!read_bounded_string(
                body, "model", manager_->get_current_model(),
                limits::kMaximumStringBytes, true, model, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        RouteRequest scheduled_request;
        scheduled_request.requested_model = model;
        scheduled_request.modality = "text";
        const auto scheduled_route = choose_scheduled_model(scheduled_request);
        if (!scheduled_route.accepted) {
            send_error(res, scheduled_route.error, "route_rejected", 422);
            return;
        }
        model = scheduled_route.model;
        bool stream = body.value("stream", false);
        int max_tokens = 0;
        if (!read_bounded_integer(
                body, "max_tokens", 4096,
                limits::kMinimumMaxTokens, limits::kMaximumMaxTokens,
                max_tokens, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        float temperature = 0.0f;
        float top_p = 0.0f;
        int top_k = 0;
        if (!read_bounded_float(body, "temperature", 1.0, 0.0, 2.0, temperature, validation_error) ||
            !read_bounded_float(body, "top_p", 0.999, 0.0, 1.0, top_p, validation_error) ||
            !read_bounded_integer(body, "top_k", 0, 0, 1000, top_k, validation_error)) {
            send_error(res, validation_error);
            return;
        }

        // System prompt (Anthropic puts it at top level, not in messages)
        std::string system_prompt;
        if (!read_bounded_string(
                body, "system", "", limits::kMaximumPromptBytes,
                true, system_prompt, validation_error)) {
            send_error(res, validation_error);
            return;
        }

        // Extended thinking support (Anthropic feature)
        bool extended_thinking_enabled = false;
        int thinking_budget_tokens = 0;
        if (body.contains("thinking") && body["thinking"].is_object()) {
            std::string thinking_type = body["thinking"].value("type", "");
            if (thinking_type == "enabled") {
                extended_thinking_enabled = true;
                if (!read_bounded_integer(
                        body["thinking"], "budget_tokens", 1024, 100, 32000,
                        thinking_budget_tokens, validation_error)) {
                    send_error(res, validation_error);
                    return;
                }
            }
        }

        // Extract tools if provided (Anthropic tool calling)
        std::vector<json> tools;
        bool has_tools = false;
        if (body.contains("tools") && !body["tools"].is_array()) {
            send_error(res, "'tools' must be an array");
            return;
        }
        if (body.contains("tools") && body["tools"].is_array()) {
            if (body["tools"].size() > limits::kMaximumBatchItems) {
                send_error(res, "Too many tool definitions");
                return;
            }
            has_tools = true;
            for (const auto& tool : body["tools"]) {
                if (!tool.is_object()) {
                    send_error(res, "Each tool definition must be an object");
                    return;
                }
                std::string tool_name;
                std::string tool_description;
                if (!read_bounded_string(
                        tool, "name", "", limits::kMaximumStringBytes, false,
                        tool_name, validation_error) ||
                    !read_bounded_string(
                        tool, "description", "", limits::kMaximumMessageBytes, true,
                        tool_description, validation_error)) {
                    send_error(res, validation_error);
                    return;
                }
                if (tool.contains("input_schema") &&
                    tool["input_schema"].dump().size() > limits::kMaximumMessageBytes) {
                    send_error(res, "Tool input schema exceeds the request limits");
                    return;
                }
                tools.push_back(tool);
            }
        }

        // Build prompt from messages
        std::string prompt;
        if (!system_prompt.empty()) {
            prompt = "System: " + system_prompt + "\n\n";
        }

        // Add tool definitions to system prompt if tools are provided
        if (has_tools && !tools.empty()) {
            prompt += "You have access to the following tools:\n\n";
            for (const auto& tool : tools) {
                std::string tool_name = tool.value("name", "");
                std::string tool_desc = tool.value("description", "");
                prompt += "Tool: " + tool_name + "\n";
                prompt += "Description: " + tool_desc + "\n";
                if (tool.contains("input_schema")) {
                    prompt += "Parameters: " + tool["input_schema"].dump() + "\n";
                }
                prompt += "\n";
            }
            prompt += "To use a tool, respond with a JSON object in this exact format:\n";
            prompt += "```tool_call\n{\"name\": \"tool_name\", \"input\": {\"param1\": \"value1\"}}\n```\n\n";
            prompt += "Only use tools when necessary. If you can answer without tools, do so directly.\n\n";
            if (prompt.size() > limits::kMaximumPromptBytes) {
                send_error(res, "Tool definitions exceed the prompt limit");
                return;
            }
        }

        // Add extended thinking instructions if enabled
        if (extended_thinking_enabled) {
            prompt += "EXTENDED THINKING MODE ENABLED (budget: " + std::to_string(thinking_budget_tokens) + " tokens)\n\n";
            prompt += "Before providing your final response, you MUST think through the problem step by step.\n";
            prompt += "Your thinking process should be wrapped in <thinking> tags like this:\n";
            prompt += "<thinking>\n[Your detailed step-by-step reasoning here...]\n</thinking>\n\n";
            prompt += "After your thinking, provide your final response.\n";
            prompt += "The thinking section helps you work through complex problems methodically.\n\n";
        }

        if (body.contains("messages") && body["messages"].is_array() &&
            limits::is_valid_message_count(body["messages"].size())) {
            std::size_t prompt_bytes = prompt.size();
            for (const auto& msg : body["messages"]) {
                if (!msg.is_object() || !msg.contains("content") ||
                    (msg.contains("role") && !msg["role"].is_string())) {
                    send_error(res, "Each message must contain valid 'role' and 'content' fields");
                    return;
                }
                std::string role = msg.value("role", "user");

                // Anthropic content can be string or array of content blocks
                std::string content;
                if (msg["content"].is_string()) {
                    content = msg["content"].get<std::string>();
                } else if (msg["content"].is_array()) {
                    if (msg["content"].size() > limits::kMaximumMessages) {
                        send_error(res, "Too many content blocks in a message");
                        return;
                    }
                    // Array of content blocks (text, image, tool_use, tool_result, etc.)
                    for (const auto& block : msg["content"]) {
                        if (!block.is_object() ||
                            (block.contains("type") && !block["type"].is_string())) {
                            send_error(res, "Each content block must be an object with a string 'type'");
                            return;
                        }
                        std::string block_type = block.value("type", "");
                        if (block_type == "text") {
                            std::string text;
                            if (!read_bounded_string(
                                    block, "text", "",
                                    limits::kMaximumMessageBytes, true,
                                    text, validation_error)) {
                                send_error(res, validation_error);
                                return;
                            }
                            content += text;
                        } else if (block_type == "image") {
                            send_error(
                                res,
                                "Image content blocks are not supported by the Messages endpoint",
                                "not_supported",
                                501);
                            return;
                        } else if (block_type == "tool_use") {
                            // Assistant's tool call
                            std::string tool_name = block.value("name", "");
                            json tool_input = block.value("input", json::object());
                            content += "\n```tool_call\n{\"name\": \"" + tool_name + "\", \"input\": " + tool_input.dump() + "}\n```\n";
                        } else if (block_type == "tool_result") {
                            // User's tool result
                            std::string tool_content = block.value("content", "");
                            content += "\nTool Result: " + tool_content + "\n";
                        }
                    }
                } else {
                    send_error(res, "Message 'content' must be a string or array");
                    return;
                }
                if (!limits::is_valid_message_size(content.size(), true) ||
                    prompt_bytes > limits::kMaximumPromptBytes - content.size()) {
                    send_error(res, "Message content exceeds the request limits");
                    return;
                }
                prompt_bytes += content.size();

                if (role == "user") {
                    prompt += "\n\nHuman: " + content;
                } else if (role == "assistant") {
                    prompt += "\n\nAssistant: " + content;
                }
                if (prompt.size() > limits::kMaximumPromptBytes) {
                    send_error(res, "Combined prompt exceeds the request limits");
                    return;
                }
            }
            prompt += "\n\nAssistant:";
        } else {
            json error = {
                {"type", "error"},
                {"error", {
                    {"type", "invalid_request_error"},
                    {"message", "'messages' must be a non-empty bounded array"}
                }}
            };
            res.status = 400;
            res.set_content(error.dump(), MIMETYPE_JSON);
            return;
        }

        // Resolve the request model without mutating process-wide selection.
        std::string current_model = model.empty() ? manager_->get_current_model() : model;
        model_request_started(current_model);
        InferenceGateGuard model_metrics_guard(true, [this, current_model]() {
            model_request_finished(current_model);
        });
        if (current_model.empty()) {
            json error = {
                {"type", "error"},
                {"error", {
                    {"type", "invalid_request_error"},
                    {"message", "No model loaded. Load a model first."}
                }}
            };
            res.status = 400;
            res.set_content(error.dump(), MIMETYPE_JSON);
            return;
        }
        if (!manager_->is_loaded(current_model)) {
            json error = {{"type", "error"}, {"error", {{"type", "not_found_error"},
                {"message", "Model not loaded: " + current_model}}}};
            res.status = 404;
            res.set_content(error.dump(), MIMETYPE_JSON);
            return;
        }

        // Generate message ID (Anthropic format: msg_...)
        std::string message_id = "msg_" + generate_completion_id().substr(9);

        if (stream) {
            // SSE Streaming response (Anthropic format)
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");

            res.set_chunked_content_provider(
                MIMETYPE_SSE,
                [this, prompt, max_tokens, temperature, top_p, top_k,
                 message_id, current_model](size_t /*offset*/, httplib::DataSink& sink) {

                    // Send message_start event
                    json message_start = {
                        {"type", "message_start"},
                        {"message", {
                            {"id", message_id},
                            {"type", "message"},
                            {"role", "assistant"},
                            {"content", json::array()},
                            {"model", current_model},
                            {"stop_reason", nullptr},
                            {"stop_sequence", nullptr},
                            {"usage", {
                                {"input_tokens", estimate_tokens(prompt)},
                                {"output_tokens", 0}
                            }}
                        }}
                    };
                    std::string start_data = "event: message_start\ndata: " + message_start.dump() + "\n\n";
                    sink.write(start_data.data(), start_data.size());

                    // Send content_block_start
                    json block_start = {
                        {"type", "content_block_start"},
                        {"index", 0},
                        {"content_block", {
                            {"type", "text"},
                            {"text", ""}
                        }}
                    };
                    std::string block_start_data = "event: content_block_start\ndata: " + block_start.dump() + "\n\n";
                    sink.write(block_start_data.data(), block_start_data.size());

                    // Generate with streaming callback
                    int output_tokens = 0;
                    auto stream_start = std::chrono::high_resolution_clock::now();
                    manager_->generate_streaming_for_model(
                        current_model, prompt,
                        [&sink, &output_tokens](
                            const std::string& token, int /*token_id*/, bool is_eos) -> bool {

                            if (!sink.is_writable()) {
                                return false;  // Client disconnected
                            }

                            output_tokens++;

                            // Send content_block_delta
                            json delta = {
                                {"type", "content_block_delta"},
                                {"index", 0},
                                {"delta", {
                                    {"type", "text_delta"},
                                    {"text", token}
                                }}
                            };

                            std::string delta_data = "event: content_block_delta\ndata: " + delta.dump() + "\n\n";
                            sink.write(delta_data.data(), delta_data.size());

                            return !is_eos;  // Continue if not end of sequence
                        },
                        static_cast<size_t>(max_tokens),
                        temperature, top_p, top_k > 0 ? top_k : 40, 1.1f
                    );
                    auto stream_end = std::chrono::high_resolution_clock::now();
                    double latency_ms = std::chrono::duration<double, std::milli>(stream_end - stream_start).count();

                    total_tokens_ += output_tokens;
                    record_model_metrics(current_model, static_cast<uint64_t>(output_tokens), latency_ms);

                    // Send content_block_stop
                    json block_stop = {
                        {"type", "content_block_stop"},
                        {"index", 0}
                    };
                    std::string block_stop_data = "event: content_block_stop\ndata: " + block_stop.dump() + "\n\n";
                    sink.write(block_stop_data.data(), block_stop_data.size());

                    // Send message_delta with final usage
                    json message_delta = {
                        {"type", "message_delta"},
                        {"delta", {
                            {"stop_reason", "end_turn"},
                            {"stop_sequence", nullptr}
                        }},
                        {"usage", {
                            {"output_tokens", output_tokens}
                        }}
                    };
                    std::string delta_final = "event: message_delta\ndata: " + message_delta.dump() + "\n\n";
                    sink.write(delta_final.data(), delta_final.size());

                    // Send message_stop
                    json message_stop = {{"type", "message_stop"}};
                    std::string stop_data = "event: message_stop\ndata: " + message_stop.dump() + "\n\n";
                    sink.write(stop_data.data(), stop_data.size());

                    sink.done();
                    return false;  // Done
                }
            );

        } else {
            // Non-streaming response (Anthropic format)
            auto start_time = std::chrono::high_resolution_clock::now();

            size_t actual_tokens = 0;
            std::string result = manager_->generate_for_model(
                current_model, prompt, static_cast<size_t>(max_tokens), &actual_tokens,
                temperature, top_p, top_k > 0 ? top_k : 40, 1.1f
            );

            auto end_time = std::chrono::high_resolution_clock::now();
            double generation_time = std::chrono::duration<double>(end_time - start_time).count();

            int input_tokens = estimate_tokens(prompt);
            int output_tokens = static_cast<int>(actual_tokens);
            if (output_tokens == 0) {
                output_tokens = estimate_tokens(result);
            }
            double latency_ms = generation_time * 1000.0;

            total_tokens_ += output_tokens;
            record_model_metrics(current_model, static_cast<uint64_t>(output_tokens), latency_ms);

            // Check if the response contains a tool call or thinking
            json content_array = json::array();
            std::string stop_reason = "end_turn";

            // Process extended thinking if enabled - look for <thinking> tags
            std::string remaining_result = result;
            if (extended_thinking_enabled) {
                size_t think_start = result.find("<thinking>");
                size_t think_end = result.find("</thinking>");

                if (think_start != std::string::npos && think_end != std::string::npos && think_end > think_start) {
                    // Extract thinking content
                    std::string thinking_content = result.substr(think_start + 10, think_end - think_start - 10);

                    // Trim whitespace
                    while (!thinking_content.empty() && (thinking_content.front() == '\n' || thinking_content.front() == ' ')) {
                        thinking_content.erase(0, 1);
                    }
                    while (!thinking_content.empty() && (thinking_content.back() == '\n' || thinking_content.back() == ' ')) {
                        thinking_content.pop_back();
                    }

                    // Add thinking block to content array
                    content_array.push_back({
                        {"type", "thinking"},
                        {"thinking", thinking_content}
                    });

                    // Get the text after thinking
                    remaining_result = result.substr(think_end + 11);
                    // Trim leading whitespace
                    while (!remaining_result.empty() && (remaining_result.front() == '\n' || remaining_result.front() == ' ')) {
                        remaining_result.erase(0, 1);
                    }

                    std::cout << "[Server] Extended thinking: " << thinking_content.length() << " chars" << std::endl;
                }
            }

            // Look for ALL tool_call patterns in the result (batch tool calling support)
            // Parse multiple ```tool_call blocks from the response
            std::string search_text = remaining_result;
            size_t search_pos = 0;
            bool found_any_tool = false;
            bool first_tool = true;
            int tool_count = 0;

            while (true) {
                size_t tool_start = search_text.find("```tool_call", search_pos);
                if (tool_start == std::string::npos) break;

                size_t tool_end = search_text.find("```", tool_start + 12);
                if (tool_end == std::string::npos) break;

                // For the first tool, capture any text before it
                if (first_tool && tool_start > 0) {
                    std::string before_tool = search_text.substr(0, tool_start);
                    // Trim whitespace
                    while (!before_tool.empty() && (before_tool.back() == '\n' || before_tool.back() == ' ')) {
                        before_tool.pop_back();
                    }
                    if (!before_tool.empty()) {
                        content_array.push_back({
                            {"type", "text"},
                            {"text", before_tool}
                        });
                    }
                    first_tool = false;
                }

                // Extract the tool call JSON
                std::string tool_json_str = search_text.substr(tool_start + 13, tool_end - tool_start - 13);

                // Trim whitespace
                while (!tool_json_str.empty() && (tool_json_str.front() == '\n' || tool_json_str.front() == ' ')) {
                    tool_json_str.erase(0, 1);
                }
                while (!tool_json_str.empty() && (tool_json_str.back() == '\n' || tool_json_str.back() == ' ')) {
                    tool_json_str.pop_back();
                }

                try {
                    json tool_call = json::parse(tool_json_str);
                    std::string tool_name = tool_call.value("name", "");
                    json tool_input = tool_call.value("input", json::object());

                    // Generate unique tool use ID with counter for batch calls
                    std::string base_id = generate_completion_id().substr(9);
                    std::string tool_use_id = "toolu_" + base_id;
                    if (tool_count > 0) {
                        tool_use_id += "_" + std::to_string(tool_count);
                    }

                    content_array.push_back({
                        {"type", "tool_use"},
                        {"id", tool_use_id},
                        {"name", tool_name},
                        {"input", tool_input}
                    });

                    found_any_tool = true;
                    tool_count++;
                    stop_reason = "tool_use";

                    std::cout << "[Server] Tool call " << tool_count << " detected: " << tool_name << std::endl;
                } catch (const json::exception& e) {
                    std::cerr << "[Server] Failed to parse tool call: " << e.what() << std::endl;
                }

                // Move search position past this tool call
                search_pos = tool_end + 3;
            }

            // If no tools were found, treat as regular text
            if (!found_any_tool) {
                if (!remaining_result.empty()) {
                    content_array.push_back({
                        {"type", "text"},
                        {"text", remaining_result}
                    });
                }
            } else if (tool_count > 1) {
                std::cout << "[Server] Batch tool calling: " << tool_count << " tools requested" << std::endl;
            }

            // Anthropic response format
            json response = {
                {"id", message_id},
                {"type", "message"},
                {"role", "assistant"},
                {"content", content_array},
                {"model", current_model},
                {"stop_reason", stop_reason},
                {"stop_sequence", nullptr},
                {"usage", {
                    {"input_tokens", input_tokens},
                    {"output_tokens", output_tokens}
                }}
            };

            send_json(res, response.dump());

            std::cout << "[Server] Anthropic Messages API: Generated " << output_tokens
                      << " tokens in " << generation_time << "s" << std::endl;
        }

    } catch (const json::exception&) {
        json error = {
            {"type", "error"},
            {"error", {
                {"type", "invalid_request_error"},
                {"message", "Invalid JSON request body"}
            }}
        };
        res.status = 400;
        res.set_content(error.dump(), MIMETYPE_JSON);
    } catch (const std::exception& e) {
        std::cerr << "[SnapLLM Server] messages request failed: "
                  << e.what() << std::endl;
        json error = {
            {"type", "error"},
            {"error", {
                {"type", "api_error"},
                {"message", "Internal server error"}
            }}
        };
        res.status = 500;
        res.set_content(error.dump(), MIMETYPE_JSON);
    }
}

// ============================================================================
// Model Management Endpoints
// ============================================================================

void SnapLLMServer::handle_config_update(const httplib::Request& req, httplib::Response& res) {
    try {
        json body = json::parse(req.body.empty() ? "{}" : req.body);

        json merged = body;
        const char* sections[] = {"server", "workspace", "runtime"};
        for (const auto* section : sections) {
            if (body.contains(section) && body[section].is_object()) {
                for (auto& [key, value] : body[section].items()) {
                    merged[key] = value;
                }
            }
        }

        ServerConfig updated = config_;
        std::vector<std::string> updated_fields;
        std::vector<std::string> restart_required;

        auto update_string = [&](const char* key, std::string& target, bool allow_empty) {
            if (!merged.contains(key)) return true;
            if (!merged[key].is_string()) {
                send_error(res, std::string("Invalid type for '") + key + "'", "invalid_request_error", 400);
                return false;
            }
            std::string value = merged[key].get<std::string>();
            if (!allow_empty && value.empty()) {
                send_error(res, std::string("'") + key + "' cannot be empty", "invalid_request_error", 400);
                return false;
            }
            if (target != value) {
                target = value;
                updated_fields.push_back(key);
                restart_required.push_back(key);
            }
            return true;
        };

        auto update_int = [&](const char* key, int& target, int min_value, int max_value) {
            if (!merged.contains(key)) return true;
            if (!merged[key].is_number_integer()) {
                send_error(res, std::string("Invalid type for '") + key + "'", "invalid_request_error", 400);
                return false;
            }
            int value = merged[key].get<int>();
            if (value < min_value || value > max_value) {
                send_error(res, std::string("'") + key + "' must be between " + std::to_string(min_value) + " and " + std::to_string(max_value), "invalid_request_error", 400);
                return false;
            }
            if (target != value) {
                target = value;
                updated_fields.push_back(key);
                restart_required.push_back(key);
            }
            return true;
        };

        auto update_bool = [&](const char* key, bool& target) {
            if (!merged.contains(key)) return true;
            if (!merged[key].is_boolean()) {
                send_error(res, std::string("Invalid type for '") + key + "'", "invalid_request_error", 400);
                return false;
            }
            bool value = merged[key].get<bool>();
            if (target != value) {
                target = value;
                updated_fields.push_back(key);
                restart_required.push_back(key);
            }
            return true;
        };

        if (!update_string("host", updated.host, false)) return;
        if (!update_int("port", updated.port, 1, 65535)) return;
        if (!update_string("workspace_root", updated.workspace_root, false)) return;
        if (!update_string("default_models_path", updated.default_models_path, false)) return;
        if (!update_bool("cors_enabled", updated.cors_enabled)) return;
        if (!update_int("timeout_seconds", updated.timeout_seconds, 30, 86400)) return;
        if (!update_int("max_concurrent_requests", updated.max_concurrent_requests, 1, 128)) return;
        if (updated.max_active_inferences > updated.max_concurrent_requests) {
            send_error(res, "max_concurrent_requests cannot be lower than max_active_inferences", "invalid_request_error", 400);
            return;
        }
        if (merged.contains("max_active_inferences")) {
            if (!merged["max_active_inferences"].is_number_integer()) {
                send_error(res, "Invalid type for 'max_active_inferences'", "invalid_request_error", 400);
                return;
            }
            const int value = merged["max_active_inferences"].get<int>();
            if (value < 1 || value > updated.max_concurrent_requests) {
                send_error(res, "'max_active_inferences' must be between 1 and max_concurrent_requests", "invalid_request_error", 400);
                return;
            }
            updated.max_active_inferences = value;
            updated_fields.push_back("max_active_inferences");
        }
        if (!update_int("max_models", updated.max_models, 1, 64)) return;
        if (!update_int("default_ram_budget_mb", updated.default_ram_budget_mb, 512, 1048576)) return;
        if (merged.contains("default_strategy")) {
            if (!merged["default_strategy"].is_string()) {
                send_error(res, "Invalid type for 'default_strategy'", "invalid_request_error", 400);
                return;
            }
            std::string strategy = merged["default_strategy"].get<std::string>();
            std::string normalized = strategy;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
            const std::vector<std::string> allowed = {"balanced", "conservative", "aggressive", "performance"};
            if (std::find(allowed.begin(), allowed.end(), normalized) == allowed.end()) {
                send_error(res, "Invalid value for 'default_strategy'", "invalid_request_error", 400);
                return;
            }
            if (updated.default_strategy != normalized) {
                updated.default_strategy = normalized;
                updated_fields.push_back("default_strategy");
                restart_required.push_back("default_strategy");
            }
        }
        if (!update_bool("enable_gpu", updated.enable_gpu)) return;

        if (!security::is_valid_bind_host(updated.host)) {
            send_error(res, "Invalid server host", "invalid_request_error", 400);
            return;
        }
        if (!security::is_loopback_host(updated.host) && config_.api_key.empty()) {
            send_error(
                res,
                "A strong API key is required before configuring a non-loopback host",
                "invalid_request_error",
                400);
            return;
        }

        std::string error;
        json payload = build_persisted_config(updated);
        if (!write_config_file(config_.config_path.empty() ? get_default_config_path() : config_.config_path, payload, error)) {
            send_error(res, "Failed to persist configuration: " + error, "server_error", 500);
            return;
        }

        if (updated.max_active_inferences != config_.max_active_inferences) {
            config_.max_active_inferences = updated.max_active_inferences;
            {
                std::lock_guard<std::mutex> lock(inference_gate_mutex_);
                max_active_inferences_ = updated.max_active_inferences;
            }
            if (auto bridge = manager_->get_bridge()) bridge->set_max_concurrent_inferences(updated.max_active_inferences);
        }

        json response = {
            {"status", "success"},
            {"updated_fields", updated_fields},
            {"restart_required", !restart_required.empty()},
            {"restart_required_fields", restart_required},
            {"config_path", config_.config_path.empty() ? get_default_config_path() : config_.config_path}
        };
        send_json(res, response.dump());
    } catch (const json::exception&) {
        send_error(res, "Invalid JSON request body", "invalid_request_error", 400);
    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

void SnapLLMServer::handle_load_model(const httplib::Request& req, httplib::Response& res) {
    try {
        json body = json::parse(req.body);
        if (!body.is_object()) {
            send_error(res, "Request body must be a JSON object");
            return;
        }

        // Support both "name"/"path" and "model_id"/"file_path" formats
        std::string validation_error;
        std::string name;
        std::string path;
        std::string model_type_str;
        if (!read_bounded_string_alias(
                body, "name", "model_id", "", limits::kMaximumStringBytes,
                false, name, validation_error) ||
            !read_bounded_string_alias(
                body, "path", "file_path", "", limits::kMaximumStringBytes,
                false, path, validation_error) ||
            !read_bounded_string(
                body, "model_type", "auto", limits::kMaximumStringBytes,
                false, model_type_str, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        if (!limits::is_valid_identifier_component(name)) {
            send_error(res, "'name' must be a safe model identifier");
            return;
        }

        // Multi-file model paths (for SD3, FLUX, Wan2)
        std::string vae_path;
        std::string t5xxl_path;
        std::string clip_l_path;
        std::string clip_g_path;
        std::string clip_vision_path;
        std::string high_noise_model_path;

        // Vision model paths (for Gemma, Qwen, etc.)
        std::string mmproj_path;
        if (!read_bounded_string(
                body, "vae_path", "", limits::kMaximumStringBytes,
                true, vae_path, validation_error) ||
            !read_bounded_string(
                body, "t5xxl_path", "", limits::kMaximumStringBytes,
                true, t5xxl_path, validation_error) ||
            !read_bounded_string(
                body, "clip_l_path", "", limits::kMaximumStringBytes,
                true, clip_l_path, validation_error) ||
            !read_bounded_string(
                body, "clip_g_path", "", limits::kMaximumStringBytes,
                true, clip_g_path, validation_error) ||
            !read_bounded_string(
                body, "clip_vision_path", "", limits::kMaximumStringBytes,
                true, clip_vision_path, validation_error) ||
            !read_bounded_string(
                body, "high_noise_model_path", "", limits::kMaximumStringBytes,
                true, high_noise_model_path, validation_error) ||
            !read_bounded_string_alias(
                body, "mmproj_path", "vision_projector", "",
                limits::kMaximumStringBytes, true, mmproj_path,
                validation_error)) {
            send_error(res, validation_error);
            return;
        }

        const auto roots = request_path_roots(config_);
        auto confine_path = [&](const char* label, std::string& value) {
            if (value.empty()) {
                return true;
            }
            auto canonical =
                limits::canonical_path_within_roots(roots, fs::path(value));
            if (!canonical) {
                validation_error = std::string("'") + label +
                                   "' must be within the configured model or workspace root";
                return false;
            }
            value = canonical->string();
            return true;
        };
        if (!confine_path("path", path) ||
            !confine_path("vae_path", vae_path) ||
            !confine_path("t5xxl_path", t5xxl_path) ||
            !confine_path("clip_l_path", clip_l_path) ||
            !confine_path("clip_g_path", clip_g_path) ||
            !confine_path("clip_vision_path", clip_vision_path) ||
            !confine_path("high_noise_model_path", high_noise_model_path) ||
            !confine_path("mmproj_path", mmproj_path)) {
            send_error(res, validation_error, "invalid_path", 400);
            return;
        }
        bool cache_only = false;
        if (body.contains("cache_only")) {
            if (!body["cache_only"].is_boolean()) {
                send_error(res, "'cache_only' must be a boolean");
                return;
            }
            cache_only = body["cache_only"].get<bool>();
        }
        if (cache_only) {
            send_error(
                res,
                "Cache-only model loading is not implemented",
                "not_supported",
                501);
            return;
        }
        std::string strategy;
        if (!read_bounded_string(
                body, "strategy", "auto", 16, false, strategy,
                validation_error)) {
            send_error(res, validation_error);
            return;
        }
        int requested_gpu_layers = -1;
        if (strategy == "cpu") {
            requested_gpu_layers = 0;
        } else if (strategy == "gpu") {
            requested_gpu_layers = 999;
        } else if (strategy != "auto") {
            send_error(res, "'strategy' must be auto, cpu, or gpu");
            return;
        }
        if (!read_bounded_integer(
                body, "n_gpu_layers", requested_gpu_layers, -1, 999,
                requested_gpu_layers, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        if (!fs::exists(path)) {
            send_error(res, "Model file not found: " + path, "not_found", 404);
            return;
        }

        // Detect model type if auto
        ModelType detected_type = ModelType::TEXT_LLM;
        if (model_type_str == "auto") {
            detected_type = detect_model_type(path);
        } else if (model_type_str == "llm" || model_type_str == "text") {
            detected_type = ModelType::TEXT_LLM;
        } else if (model_type_str == "diffusion" || model_type_str == "sd") {
            detected_type = ModelType::IMAGE_DIFFUSION;
        } else if (model_type_str == "vision" || model_type_str == "vl") {
            detected_type = ModelType::MULTIMODAL_VL;
        } else if (model_type_str == "video") {
            detected_type = ModelType::VIDEO_DIFFUSION;
        } else {
            send_error(
                res,
                "'model_type' must be auto, llm, text, diffusion, sd, vision, vl, or video");
            return;
        }

        // Check if this is a multi-file diffusion model (SD3, FLUX, Wan2)
        bool is_multifile = !vae_path.empty() && !t5xxl_path.empty();
        if (is_multifile) {
            const std::vector<std::pair<std::string, std::string>> required_paths = {
                {"vae_path", vae_path},
                {"t5xxl_path", t5xxl_path},
                {"clip_l_path", clip_l_path},
                {"clip_g_path", clip_g_path},
                {"clip_vision_path", clip_vision_path},
                {"high_noise_model_path", high_noise_model_path}
            };
            for (const auto& [label, p] : required_paths) {
                if (!p.empty() && !fs::exists(p)) {
                    send_error(res, "Missing " + label + ": " + p, "not_found", 404);
                    return;
                }
            }
        }
        if (!mmproj_path.empty() && !fs::exists(mmproj_path)) {
            send_error(res, "Vision projector not found: " + mmproj_path, "not_found", 404);
            return;
        }
#ifdef SNAPLLM_HAS_MULTIMODAL
        if (!mmproj_path.empty()) {
            std::string proj_type = read_mmproj_projector_type(mmproj_path);
            if (!proj_type.empty() && !is_supported_projector_type(proj_type)) {
                std::string supported = format_supported_projector_types();
                send_error(
                    res,
                    "Unsupported multimodal projector type '" + proj_type +
                        "'. Supported types: " + supported,
                    "not_supported",
                    400
                );
                return;
            }
        }
#endif

        std::cout << "[Server] Loading model '" << name << "' from " << path << std::endl;
        std::cout << "[Server] Model type string: '" << model_type_str << "'" << std::endl;
        std::cout << "[Server] Detected type: " << model_type_to_string(detected_type) << std::endl;
        if (detected_type == ModelType::VIDEO_DIFFUSION) {
            send_error(res, "Video models are not supported in this build", "not_supported", 501);
            return;
        }
        if (is_multifile) {
            std::cout << "[Server] Multi-file model detected:" << std::endl;
            std::cout << "[Server]   VAE: " << vae_path << std::endl;
            std::cout << "[Server]   T5XXL: " << t5xxl_path << std::endl;
            if (!clip_l_path.empty()) std::cout << "[Server]   CLIP-L: " << clip_l_path << std::endl;
            if (!clip_g_path.empty()) std::cout << "[Server]   CLIP-G: " << clip_g_path << std::endl;
            if (!clip_vision_path.empty()) std::cout << "[Server]   CLIP-Vision: " << clip_vision_path << std::endl;
        }
        if (!mmproj_path.empty()) {
            std::cout << "[Server] Vision projector: " << mmproj_path << std::endl;
        }
        std::cout.flush();

        auto start_time = std::chrono::high_resolution_clock::now();
        bool success = false;

#ifdef SNAPLLM_HAS_DIFFUSION
        std::cout << "[Server] DIFFUSION SUPPORT ENABLED" << std::endl;
        // Route diffusion models to shared DiffusionBridge
        if (detected_type == ModelType::IMAGE_DIFFUSION || detected_type == ModelType::VIDEO_DIFFUSION) {
            std::cout << "[Server] Routing to DiffusionBridge..." << std::endl;
            auto* diffusion_bridge = get_diffusion_bridge(config_.workspace_root);

            if (is_multifile) {
                // Use multi-file loading for SD3/FLUX/Wan2
                MultiFileModelParams params;
                params.model_name = name;
                params.diffusion_model_path = path;
                params.vae_path = vae_path;
                params.t5xxl_path = t5xxl_path;
                params.clip_l_path = clip_l_path;
                params.clip_g_path = clip_g_path;
                params.clip_vision_path = clip_vision_path;
                params.high_noise_model_path = high_noise_model_path;
                params.offload_to_cpu = body.value("offload_to_cpu", true);

                success = diffusion_bridge->load_multifile_model(params);
            } else {
                // Single-file model (SD1.5, SDXL single-file)
                success = diffusion_bridge->load_model(name, path, vae_path, false);
            }

            if (success) {
                std::cout << "[Server] Diffusion model loaded via DiffusionBridge" << std::endl;
            } else {
                std::cout << "[Server] DiffusionBridge failed to load model" << std::endl;
            }
        } else
#else
        std::cout << "[Server] DIFFUSION SUPPORT DISABLED - compiling without SNAPLLM_HAS_DIFFUSION" << std::endl;
#endif
        {
#ifdef SNAPLLM_HAS_MULTIMODAL
            // Check if this is a vision/multimodal model with projector
            if (!mmproj_path.empty() && detected_type == ModelType::MULTIMODAL_VL) {
                std::cout << "[Server] Routing to MultimodalBridge (vision model)..." << std::endl;
                auto* multimodal_bridge = get_multimodal_bridge();

                // Configure multimodal model
                MultimodalConfig mm_config;
                mm_config.model_path = path;
                mm_config.mmproj_path = mmproj_path;
                int multimodal_gpu_layers = 0;
                int multimodal_context_size = 0;
                int multimodal_threads = 0;
                if (!read_bounded_integer(
                        body, "n_gpu_layers", -1, -1, 999,
                        multimodal_gpu_layers, validation_error) ||
                    !read_bounded_integer(
                        body, "ctx_size", 4096, 128, 131072,
                        multimodal_context_size, validation_error) ||
                    !read_bounded_integer(
                        body, "n_threads", 4, 1, 256,
                        multimodal_threads, validation_error)) {
                    send_error(res, validation_error);
                    return;
                }
                mm_config.n_gpu_layers = multimodal_gpu_layers;
                mm_config.ctx_size = multimodal_context_size;
                mm_config.n_threads = multimodal_threads;
                mm_config.use_gpu = body.value("use_gpu", true);

                success = multimodal_bridge->load_model(mm_config);

                if (success) {
                    std::cout << "[Server] Vision model loaded via MultimodalBridge" << std::endl;
                    std::cout << "[Server] Vision support: " << (multimodal_bridge->supports_vision() ? "yes" : "no") << std::endl;
                } else {
                    std::cout << "[Server] MultimodalBridge failed to load vision model" << std::endl;
                }
            } else
#endif
            {
                std::cout << "[Server] Routing to ModelManager (llama.cpp)..." << std::endl;
                // Regular LLM models go through ModelManager
                success = manager_->load_model(
                    name, path, false, DomainType::General,
                    GPUConfig::with_layers(requested_gpu_layers));
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        double load_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

        if (success) {
            bool active = false;
            if (detected_type == ModelType::TEXT_LLM) {
                active = manager_->switch_model(name);
                if (!active) {
                    send_error(
                        res,
                        "Model loaded but could not be made ready: " + name,
                        "activation_failed",
                        500);
                    return;
                }
                std::cout << "[Server] Auto-switched to model: " << name << std::endl;
            }

            json response = {
                {"status", "success"},
                {"message", "Model loaded: " + name},
                {"model", name},
                {"model_type", model_type_to_string(detected_type)},
                {"load_time_ms", load_time_ms},
                {"cache_only", cache_only},
                {"strategy", requested_gpu_layers == 0
                    ? "cpu"
                    : (requested_gpu_layers == 999 ? "gpu" : "auto")},
                {"n_gpu_layers", requested_gpu_layers},
                {"active", active}
            };
            send_json(res, response.dump(), 201);
            std::cout << "[Server] Model '" << name << "' loaded in " << load_time_ms << "ms" << std::endl;
        } else {
            if (detected_type == ModelType::MULTIMODAL_VL && !mmproj_path.empty()) {
#ifdef SNAPLLM_HAS_MULTIMODAL
                std::string supported = format_supported_projector_types();
                send_error(
                    res,
                    "Failed to load vision model: " + name +
                        ". Ensure the projector is compatible. Supported types: " + supported,
                    "load_failed",
                    500
                );
#else
                send_error(res, "Vision support not enabled in this build", "not_supported", 501);
#endif
            } else {
                send_error(res, "Failed to load model: " + name, "load_failed", 500);
            }
        }

    } catch (const json::exception&) {
        send_error(res, "Invalid JSON request body");
    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

void SnapLLMServer::handle_switch_model(const httplib::Request& req, httplib::Response& res) {
    try {
        json body = json::parse(req.body);
        // Accept both 'model_id' (API standard) and 'name' (legacy) for compatibility
        std::string name;
        std::string validation_error;
        if (!read_bounded_string_alias(
                body, "model_id", "name", "", 255,
                false, name, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        if (!limits::is_valid_identifier_component(name)) {
            send_error(res, "'model_id' must be a safe model identifier");
            return;
        }

        auto start_time = std::chrono::high_resolution_clock::now();
        bool success;
        {
            std::lock_guard<std::mutex> switch_lock(model_switch_mutex_);
            success = manager_->switch_model(name);
        }
        auto end_time = std::chrono::high_resolution_clock::now();

        double switch_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

        if (success) {
            json response = {
                {"status", "success"},
                {"message", "Switched to model: " + name},
                {"model", name},
                {"switch_time_ms", switch_time_ms}
            };
            send_json(res, response.dump());
            std::cout << "[Server] Switched to '" << name << "' in " << switch_time_ms << "ms" << std::endl;
        } else {
            send_error(res, "Model not found: " + name, "not_found", 404);
        }

    } catch (const json::exception&) {
        send_error(res, "Invalid JSON request body");
    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

void SnapLLMServer::handle_unload_model(const httplib::Request& req, httplib::Response& res) {
    try {
        json body = json::parse(req.body);
        // Accept both 'model_id' (API standard) and 'name' (legacy) for compatibility
        std::string name;
        std::string validation_error;
        if (!read_bounded_string_alias(
                body, "model_id", "name", "", 255,
                false, name, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        if (!limits::is_valid_identifier_component(name)) {
            send_error(res, "'model_id' must be a safe model identifier");
            return;
        }

        bool unloaded = false;
        std::string model_type = "llm";

#ifdef SNAPLLM_HAS_DIFFUSION
        // Check if this is a diffusion model first
        auto* diffusion_bridge = get_diffusion_bridge(config_.workspace_root);
        if (diffusion_bridge->is_model_loaded(name)) {
            diffusion_bridge->unload_model(name);
            unloaded = true;
            model_type = "diffusion";
            std::cout << "[Server] Unloaded diffusion model: " << name << std::endl;
        }
#endif

        // If not a diffusion model, try unloading from LLM manager
        if (!unloaded && manager_->is_loaded(name)) {
            manager_->unload_model(name);
            unloaded = true;
            std::cout << "[Server] Unloaded LLM model: " << name << std::endl;
        }

        if (unloaded) {
            json response = {
                {"status", "success"},
                {"message", "Model unloaded: " + name},
                {"model_type", model_type},
                {"current_model", manager_->get_current_model()}
            };
            send_json(res, response.dump());
        } else {
            send_error(res, "Model not found: " + name, "not_found", 404);
        }

    } catch (const json::exception&) {
        send_error(res, "Invalid JSON request body");
    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

// ============================================================================
// Folder Scanning Endpoint
// ============================================================================

void SnapLLMServer::handle_scan_folder(const httplib::Request& req, httplib::Response& res) {
    try {
        json body = json::parse(req.body);
        if (!body.is_object()) {
            send_error(res, "Request body must be a JSON object");
            return;
        }
        std::string path;
        std::string validation_error;
        if (!read_bounded_string(
                body, "path", "", limits::kMaximumStringBytes,
                false, path, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        auto canonical = limits::canonical_path_within_roots(
            request_path_roots(config_), fs::path(path));
        if (!canonical) {
            send_error(
                res,
                "'path' must be within the configured model or workspace root",
                "invalid_path",
                400);
            return;
        }
        path = canonical->string();

        std::cout << "[Server] Scanning folder: " << path << std::endl;

        // Check if path exists
        if (!fs::exists(path)) {
            send_error(res, "Path does not exist: " + path, "not_found", 404);
            return;
        }

        if (!fs::is_directory(path)) {
            send_error(res, "Path is not a directory: " + path, "invalid_path", 400);
            return;
        }

        // Scan for .gguf files
        constexpr size_t kMaximumScannedModels = 10000;
        constexpr auto kMaximumScanDuration = std::chrono::seconds(5);
        const auto scan_started = std::chrono::steady_clock::now();
        std::vector<json> models;
        for (const auto& entry : fs::directory_iterator(path)) {
            if (models.size() >= kMaximumScannedModels ||
                std::chrono::steady_clock::now() - scan_started > kMaximumScanDuration) {
                send_error(
                    res,
                    "Folder scan exceeded its result or time limit",
                    "scan_limit_exceeded",
                    413);
                return;
            }
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                std::string ext = entry.path().extension().string();

                // Convert extension to lowercase for comparison
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (ext == ".gguf") {
                    // Get file info
                    auto file_size = fs::file_size(entry.path());
                    auto last_write = fs::last_write_time(entry.path());

                    // Extract model name from filename (remove .gguf extension)
                    std::string model_name = filename.substr(0, filename.length() - 5);

                    // Determine quantization from filename
                    std::string quantization = "unknown";
                    std::vector<std::string> quant_types = {
                        "Q8_0", "Q6_K", "Q5_K_M", "Q5_K_S", "Q5_K", "Q5_0", "Q5_1",
                        "Q4_K_M", "Q4_K_S", "Q4_K", "Q4_0", "Q4_1",
                        "Q3_K_M", "Q3_K_S", "Q3_K_L", "Q3_K",
                        "Q2_K", "IQ4_XS", "IQ3_M", "IQ2_S", "F16", "F32"
                    };
                    for (const auto& qt : quant_types) {
                        if (filename.find(qt) != std::string::npos) {
                            quantization = qt;
                            break;
                        }
                    }

                    models.push_back({
                        {"path", entry.path().string()},
                        {"filename", filename},
                        {"name", model_name},
                        {"size_bytes", file_size},
                        {"size_gb", static_cast<double>(file_size) / (1024.0 * 1024.0 * 1024.0)},
                        {"quantization", quantization}
                    });
                }
            }
        }

        // Sort by name
        std::sort(models.begin(), models.end(), [](const json& a, const json& b) {
            return a["name"].get<std::string>() < b["name"].get<std::string>();
        });

        json response = {
            {"status", "success"},
            {"path", path},
            {"count", models.size()},
            {"models", models}
        };

        send_json(res, response.dump());
        std::cout << "[Server] Found " << models.size() << " GGUF models in " << path << std::endl;

    } catch (const json::exception&) {
        send_error(res, "Invalid JSON request body");
    } catch (const fs::filesystem_error& e) {
        send_internal_error(res, "folder scan", e);
    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

// ============================================================================
// Cache Management Endpoints
// ============================================================================

void SnapLLMServer::handle_cache_stats(const httplib::Request&, httplib::Response& res) {
    auto models = manager_->get_loaded_models();

    json models_array = json::array();
    size_t total_memory_bytes = 0;
    uint64_t total_cache_hits = 0;
    uint64_t total_cache_misses = 0;
    uint64_t total_reads = 0;
    uint64_t total_writes = 0;
    uint64_t total_bytes_read = 0;
    uint64_t total_bytes_written = 0;

    for (const auto& model : models) {
        // Get workspace for cache stats if available
        auto workspace = manager_->get_workspace(model);

        json model_stats = {
            {"model_id", model},
            {"is_current", model == manager_->get_current_model()},
            {"cache_hits", 0},
            {"cache_misses", 0},
            {"cache_hit_rate", 0.0},
            {"processing_hits", 0},
            {"processing_misses", 0},
            {"processing_hit_rate", 0.0},
            {"generation_hits", 0},
            {"generation_misses", 0},
            {"generation_hit_rate", 0.0},
            {"total_reads", 0},
            {"total_writes", 0},
            {"bytes_read", 0},
            {"bytes_written", 0},
            {"workspace_total_mb", 0},
            {"workspace_used_mb", 0},
            {"workspace_utilization", 0.0},
            {"tensor_cache_used_mb", 0},
            {"tensor_cache_budget_mb", 0},
            {"tensor_cache_utilization", 0.0},
            {"cached_tensor_count", 0},
            {"fragmentation", 0.0},
            {"estimated_speedup", 1.0},
            {"memory_usage_mb", 0}
        };

        if (workspace) {
            // Get VPIDStats from workspace
            const auto& stats = workspace->get_stats();

            uint64_t hits = stats.cache_hits.load();
            uint64_t misses = stats.cache_misses.load();
            uint64_t reads = stats.total_reads.load();
            uint64_t writes = stats.total_writes.load();
            uint64_t br = stats.bytes_read.load();
            uint64_t bw = stats.bytes_written.load();

            model_stats["cache_hits"] = hits;
            model_stats["cache_misses"] = misses;
            model_stats["cache_hit_rate"] = stats.get_hit_rate();
            model_stats["processing_hits"] = hits;
            model_stats["processing_misses"] = misses;
            model_stats["processing_hit_rate"] = stats.get_hit_rate();
            model_stats["generation_hits"] = 0;
            model_stats["generation_misses"] = 0;
            model_stats["generation_hit_rate"] = 0.0;
            model_stats["total_reads"] = reads;
            model_stats["total_writes"] = writes;
            model_stats["bytes_read"] = br;
            model_stats["bytes_written"] = bw;
            model_stats["workspace_total_mb"] = workspace->get_total_size() / (1024 * 1024);
            model_stats["workspace_used_mb"] = workspace->get_used_size() / (1024 * 1024);
            model_stats["workspace_utilization"] = workspace->get_total_size() > 0 ?
                (double)workspace->get_used_size() / workspace->get_total_size() : 0.0;
            model_stats["fragmentation"] = workspace->get_fragmentation();

            // Get tensor cache stats if available
            auto* tensor_cache = workspace->get_tensor_cache();
            if (tensor_cache) {
                size_t cache_used = tensor_cache->get_used_bytes();
                size_t cache_budget = tensor_cache->get_budget_bytes();

                model_stats["tensor_cache_used_mb"] = cache_used / (1024 * 1024);
                model_stats["tensor_cache_budget_mb"] = cache_budget / (1024 * 1024);
                model_stats["tensor_cache_utilization"] = tensor_cache->get_utilization();
                model_stats["tensor_cache_hit_rate"] = tensor_cache->get_hit_rate();
                model_stats["cached_tensor_count"] = tensor_cache->get_cached_count();
                model_stats["memory_usage_mb"] = static_cast<int64_t>(cache_used / (1024 * 1024));

                total_memory_bytes += cache_used;
            } else {
                model_stats["memory_usage_mb"] = model_stats["workspace_used_mb"];
            }

            // Estimate speedup based on hit rate (higher hit rate = faster inference)
            double hit_rate = stats.get_hit_rate();
            model_stats["estimated_speedup"] = 1.0 + (hit_rate * 2.0);  // Up to 3x with 100% hit rate

            // Aggregate totals
            total_cache_hits += hits;
            total_cache_misses += misses;
            total_reads += reads;
            total_writes += writes;
            total_bytes_read += br;
            total_bytes_written += bw;
        }

        models_array.push_back(model_stats);
    }

    // Calculate global hit rate
    double global_hit_rate = (total_cache_hits + total_cache_misses) > 0 ?
        (double)total_cache_hits / (total_cache_hits + total_cache_misses) : 0.0;

    json response = {
        {"status", "success"},
        {"models", models_array},
        {"total_memory_mb", static_cast<int64_t>(total_memory_bytes / (1024 * 1024))},
        {"summary", {
            {"total_models", models.size()},
            {"current_model", manager_->get_current_model()},
            {"total_memory_mb", static_cast<int64_t>(total_memory_bytes / (1024 * 1024))},
            {"total_cache_hits", total_cache_hits},
            {"total_cache_misses", total_cache_misses},
            {"global_hit_rate", global_hit_rate},
            {"total_reads", total_reads},
            {"total_writes", total_writes},
            {"total_bytes_read_mb", static_cast<int64_t>(total_bytes_read / (1024 * 1024))},
            {"total_bytes_written_mb", static_cast<int64_t>(total_bytes_written / (1024 * 1024))},
            {"average_speedup", 1.0 + (global_hit_rate * 2.0)}
        }}
    };

    send_json(res, response.dump());
}

void SnapLLMServer::handle_cache_clear(const httplib::Request&, httplib::Response& res) {
    send_error(
        res,
        "Prompt-cache clearing is not implemented by this runtime",
        "not_supported",
        501);
}

void SnapLLMServer::handle_server_metrics(const httplib::Request&, httplib::Response& res) {
    // Calculate uptime
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
    int active_inferences = 0;
    int max_active_inferences = 0;
    {
        std::lock_guard<std::mutex> lock(inference_gate_mutex_);
        active_inferences = active_inference_count_;
        max_active_inferences = max_active_inferences_;
    }

    // Get model metrics
    auto models = manager_->get_loaded_models();
    json models_array = json::array();

    for (const auto& model_id : models) {
        double tps = 0.0;
        double avg_latency = 0.0;
        uint64_t requests = 0;
        uint64_t tokens = 0;
        uint32_t in_flight = 0;

        {
            std::lock_guard<std::mutex> lock(model_metrics_mutex_);
            auto it = model_metrics_.find(model_id);
            if (it != model_metrics_.end()) {
                requests = it->second.requests;
                tokens = it->second.tokens_generated;
                in_flight = it->second.in_flight;
                avg_latency = (requests > 0) ? (it->second.total_latency_ms / requests) : 0.0;
                if (it->second.total_latency_ms > 0.0) {
                    tps = (tokens / (it->second.total_latency_ms / 1000.0));
                }
            }
        }

        models_array.push_back({
            {"model_name", model_id},
            {"tokens_per_second", tps},
            {"avg_latency_ms", avg_latency},
            {"requests", requests},
            {"tokens_generated", tokens}
            ,{"in_flight", in_flight}
        });
    }

    json response = {
        {"status", "success"},
        {"total_requests", total_requests_.load()},
        {"total_tokens_generated", total_tokens_.load()},
        {"total_errors", total_errors_.load()},
        {"uptime_seconds", uptime},
        {"scheduler", {
            {"active_inferences", active_inferences},
            {"max_active_inferences", max_active_inferences},
            {"waiting_inferences", waiting_inference_count_.load(std::memory_order_relaxed)},
            {"admission", "bounded_fifo_gate"}
        }},
        {"models", models_array}
    };

    send_json(res, response.dump());
}

// ============================================================================
// Text Generation Endpoints (non-chat)
// ============================================================================

void SnapLLMServer::handle_generate(const httplib::Request& req, httplib::Response& res) {
    total_requests_++;

    // === INFERENCE GATE ===
    if (!acquire_inference_gate(config_.timeout_seconds * 1000)) {
        total_errors_++;
        send_error(res, "Server busy - too many concurrent inference requests. Please retry.",
                   "server_busy", 503);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);
        if (!body.is_object()) {
            send_error(res, "Request body must be a JSON object");
            return;
        }

        std::string validation_error;
        std::string prompt;
        if (!read_bounded_string(
                body, "prompt", "", limits::kMaximumPromptBytes,
                false, prompt, validation_error)) {
            send_error(res, validation_error);
            return;
        }

        std::string model;
        if (!read_bounded_string(
                body, "model", manager_->get_current_model(),
                limits::kMaximumStringBytes, true, model, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        RouteRequest scheduled_request;
        scheduled_request.requested_model = model;
        scheduled_request.modality = "text";
        const auto scheduled_route = choose_scheduled_model(scheduled_request);
        if (!scheduled_route.accepted) {
            send_error(res, scheduled_route.error, "route_rejected", 422);
            return;
        }
        model = scheduled_route.model;
        int max_tokens = 0;
        if (!read_bounded_integer(
                body, "max_tokens", 512,
                limits::kMinimumMaxTokens, limits::kMaximumMaxTokens,
                max_tokens, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        float temperature = 0.0f;
        float top_p = 0.0f;
        float repeat_penalty = 0.0f;
        int top_k = 0;
        if (!read_bounded_float(body, "temperature", 0.8, 0.0, 2.0, temperature, validation_error) ||
            !read_bounded_float(body, "top_p", 0.95, 0.0, 1.0, top_p, validation_error) ||
            !read_bounded_integer(body, "top_k", 40, 0, 1000, top_k, validation_error) ||
            !read_bounded_float(body, "repeat_penalty", 1.1, 0.0, 10.0, repeat_penalty, validation_error)) {
            send_error(res, validation_error);
            return;
        }

        // Resolve the request model without mutating process-wide selection.
        std::string current_model = model.empty() ? manager_->get_current_model() : model;
        model_request_started(current_model);
        InferenceGateGuard model_metrics_guard(true, [this, current_model]() {
            model_request_finished(current_model);
        });
        if (current_model.empty()) {
            send_error(res, "No model loaded", "no_model", 400);
            return;
        }
        if (!manager_->is_loaded(current_model)) {
            send_error(res, "Model not loaded: " + current_model, "model_not_found", 404);
            return;
        }

        auto start_time = std::chrono::high_resolution_clock::now();

        size_t actual_tokens = 0;
        std::string result = manager_->generate_for_model(
            current_model, prompt, static_cast<size_t>(max_tokens), &actual_tokens,
            temperature, top_p, top_k, repeat_penalty
        );

        auto end_time = std::chrono::high_resolution_clock::now();
        double generation_time = std::chrono::duration<double>(end_time - start_time).count();

        int tokens = static_cast<int>(actual_tokens > 0 ? actual_tokens : estimate_tokens(result));
        double tokens_per_second = (generation_time > 0) ? (tokens / generation_time) : 0;

        total_tokens_ += tokens;
        record_model_metrics(current_model, static_cast<uint64_t>(tokens), generation_time * 1000.0);

        json response = {
            {"status", "success"},
            {"prompt", prompt},
            {"generated_text", result},
            {"model", current_model},
            {"max_tokens", max_tokens},
            {"generation_time_s", generation_time},
            {"tokens_per_second", tokens_per_second}
        };

        send_json(res, response.dump());

    } catch (const json::exception&) {
        send_error(res, "Invalid JSON request body");
    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

void SnapLLMServer::handle_generate_batch(const httplib::Request& req, httplib::Response& res) {
    total_requests_++;

    // === INFERENCE GATE ===
    if (!acquire_inference_gate(config_.timeout_seconds * 1000)) {  // request budget covers queue wait and generation
        total_errors_++;
        send_error(res, "Server busy - too many concurrent inference requests. Please retry.",
                   "server_busy", 503);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);
        if (!body.is_object()) {
            send_error(res, "Request body must be a JSON object");
            return;
        }

        // Parse global defaults
        std::string validation_error;
        std::string model;
        if (!read_bounded_string(
                body, "model", manager_->get_current_model(),
                limits::kMaximumStringBytes, true, model, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        RouteRequest scheduled_request;
        scheduled_request.requested_model = model;
        scheduled_request.modality = "text";
        const auto scheduled_route = choose_scheduled_model(scheduled_request);
        if (!scheduled_route.accepted) {
            send_error(res, scheduled_route.error, "route_rejected", 422);
            return;
        }
        model = scheduled_route.model;
        int default_max_tokens = 0;
        if (!read_bounded_integer(
                body, "max_tokens", 512,
                limits::kMinimumMaxTokens, limits::kMaximumMaxTokens,
                default_max_tokens, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        float default_temp = 0.0f;
        float default_top_p = 0.0f;
        float default_repeat = 0.0f;
        int default_top_k = 0;
        if (!read_bounded_float(body, "temperature", 0.8, 0.0, 2.0, default_temp, validation_error) ||
            !read_bounded_float(body, "top_p", 0.95, 0.0, 1.0, default_top_p, validation_error) ||
            !read_bounded_integer(body, "top_k", 40, 0, 1000, default_top_k, validation_error) ||
            !read_bounded_float(body, "repeat_penalty", 1.1, 0.0, 10.0, default_repeat, validation_error)) {
            send_error(res, validation_error);
            return;
        }

        // Build BatchPromptItem list from either "items" (new) or "prompts" (legacy) format
        std::vector<snapllm::BatchPromptItem> items;

        if (body.contains("items") && body["items"].is_array()) {
            if (!limits::is_valid_batch_count(body["items"].size())) {
                send_error(res, "'items' exceeds the batch limit");
                return;
            }
            // New rich format with per-prompt messages and parameters
            for (const auto& item_json : body["items"]) {
                if (!item_json.is_object()) {
                    send_error(res, "Each batch item must be an object");
                    return;
                }
                snapllm::BatchPromptItem item;

                if (item_json.contains("messages") && item_json["messages"].is_array()) {
                    std::size_t total_message_bytes = 0;
                    if (!validate_text_messages(
                            item_json["messages"], validation_error,
                            total_message_bytes)) {
                        send_error(res, validation_error);
                        return;
                    }
                    for (const auto& msg : item_json["messages"]) {
                        item.messages.push_back({
                            msg.value("role", "user"),
                            msg.value("content", "")
                        });
                    }
                } else if (item_json.contains("prompt")) {
                    if (!read_bounded_string(
                            item_json, "prompt", "",
                            limits::kMaximumPromptBytes, false,
                            item.raw_prompt, validation_error)) {
                        send_error(res, validation_error);
                        return;
                    }
                } else {
                    send_error(res, "Each batch item requires 'messages' or 'prompt'");
                    return;
                }

                int item_max_tokens = 0;
                if (!read_bounded_integer(
                        item_json, "max_tokens", default_max_tokens,
                        limits::kMinimumMaxTokens, limits::kMaximumMaxTokens,
                        item_max_tokens, validation_error)) {
                    send_error(res, validation_error);
                    return;
                }
                item.max_tokens = item_max_tokens;
                float item_temperature = 0.0f;
                float item_top_p = 0.0f;
                float item_repeat_penalty = 0.0f;
                int item_top_k = 0;
                if (!read_bounded_float(item_json, "temperature", default_temp, 0.0, 2.0, item_temperature, validation_error) ||
                    !read_bounded_float(item_json, "top_p", default_top_p, 0.0, 1.0, item_top_p, validation_error) ||
                    !read_bounded_integer(item_json, "top_k", default_top_k, 0, 1000, item_top_k, validation_error) ||
                    !read_bounded_float(item_json, "repeat_penalty", default_repeat, 0.0, 10.0, item_repeat_penalty, validation_error)) {
                    send_error(res, validation_error);
                    return;
                }
                item.temperature = item_temperature;
                item.top_p = item_top_p;
                item.top_k = item_top_k;
                item.repeat_penalty = item_repeat_penalty;
                if (item_json.contains("system_prompt")) {
                    std::string system_prompt;
                    if (!read_bounded_string(
                            item_json, "system_prompt", "",
                            limits::kMaximumPromptBytes, true,
                            system_prompt, validation_error)) {
                        send_error(res, validation_error);
                        return;
                    }
                    item.system_prompt = std::move(system_prompt);
                }

                items.push_back(std::move(item));
            }
        } else if (body.contains("prompts") && body["prompts"].is_array()) {
            if (!limits::is_valid_batch_count(body["prompts"].size())) {
                send_error(res, "'prompts' exceeds the batch limit");
                return;
            }
            // Legacy format: simple string array
            for (const auto& p : body["prompts"]) {
                if (!p.is_string() ||
                    !limits::is_valid_prompt_size(
                        p.get_ref<const std::string&>().size())) {
                    send_error(res, "Each prompt must be a non-empty bounded string");
                    return;
                }
                snapllm::BatchPromptItem item;
                item.raw_prompt = p.get<std::string>();
                item.max_tokens = default_max_tokens;
                items.push_back(std::move(item));
            }
        } else {
            send_error(res, "Missing 'items' or 'prompts' array in request body");
            return;
        }

        if (items.empty()) {
            send_error(res, "Empty batch request");
            return;
        }

        // Resolve the request model without mutating process-wide selection.
        std::string current_model = model.empty() ? manager_->get_current_model() : model;
        model_request_started(current_model);
        InferenceGateGuard model_metrics_guard(true, [this, current_model]() {
            model_request_finished(current_model);
        });
        if (current_model.empty()) {
            send_error(res, "No model loaded", "no_model", 400);
            return;
        }
        if (!manager_->is_loaded(current_model)) {
            send_error(res, "Model not loaded: " + current_model, "model_not_found", 404);
            return;
        }

        auto start_time = std::chrono::high_resolution_clock::now();

        // Use parallel batch processing
        std::vector<snapllm::BatchResult> results = manager_->generate_batch_for_model(
            current_model, items, default_temp, default_top_p, default_top_k, default_repeat);

        auto end_time = std::chrono::high_resolution_clock::now();
        double total_time = std::chrono::duration<double>(end_time - start_time).count();

        // Build response
        json results_array = json::array();
        int successful = 0;
        int total_generated_tokens = 0;
        for (size_t i = 0; i < results.size(); ++i) {
            json result_obj = {
                {"index", static_cast<int>(i)},
                {"generated_text", results[i].generated_text},
                {"tokens_generated", static_cast<int>(results[i].tokens_generated)},
                {"latency_ms", results[i].latency_ms},
                {"success", results[i].success}
            };
            if (!results[i].success) {
                result_obj["error"] = results[i].error;
            }
            // Include prompt info for reference
            if (i < items.size()) {
                if (!items[i].raw_prompt.empty()) {
                    result_obj["prompt"] = items[i].raw_prompt;
                } else if (!items[i].messages.empty()) {
                    result_obj["prompt"] = items[i].messages.back().content;
                }
            }
            results_array.push_back(result_obj);

            if (results[i].success) {
                successful++;
                total_generated_tokens += static_cast<int>(results[i].tokens_generated);
            }
        }

        double avg_time = (items.size() > 0) ? (total_time / items.size()) : 0;

        // Update aggregate metrics
        total_requests_.fetch_add(static_cast<uint64_t>(items.size()));
        total_tokens_ += total_generated_tokens;
        record_model_metrics(current_model, static_cast<uint64_t>(total_generated_tokens),
                             total_time * 1000.0, static_cast<uint64_t>(items.size()));

        json response = {
            {"status", "success"},
            {"results", results_array},
            {"model", current_model},
            {"total_prompts", static_cast<int>(items.size())},
            {"successful", successful},
            {"total_time_s", total_time},
            {"avg_time_per_prompt_s", avg_time},
            {"parallel_sequences", (std::min)(static_cast<int>(items.size()), 8)}
        };

        send_json(res, response.dump());

    } catch (const json::exception&) {
        send_error(res, "Invalid JSON request body");
    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

// ============================================================================
// Diffusion Endpoints (Image/Video Generation)
// ============================================================================

void SnapLLMServer::handle_diffusion_generate(const httplib::Request& req, httplib::Response& res) {
#ifdef SNAPLLM_HAS_DIFFUSION
    total_requests_++;

    // === INFERENCE GATE ===
    if (!acquire_inference_gate(config_.timeout_seconds * 1000)) {
        total_errors_++;
        send_error(res, "Server busy - too many concurrent inference requests. Please retry.",
                   "server_busy", 503);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);
        if (!body.is_object()) {
            send_error(res, "Request body must be a JSON object");
            return;
        }

        std::string validation_error;
        std::string prompt;
        std::string negative_prompt;
        std::string model;
        if (!read_bounded_string(
                body, "prompt", "", limits::kMaximumPromptBytes,
                false, prompt, validation_error) ||
            !read_bounded_string(
                body, "negative_prompt", "", limits::kMaximumPromptBytes,
                true, negative_prompt, validation_error) ||
            !read_bounded_string(
                body, "model", "", limits::kMaximumStringBytes,
                true, model, validation_error)) {
            send_error(res, validation_error);
            return;
        }

        int width = 0;
        int height = 0;
        int steps = 0;
        if (!read_bounded_integer(
                body, "width", 512,
                limits::kMinimumImageDimension, limits::kMaximumImageDimension,
                width, validation_error) ||
            !read_bounded_integer(
                body, "height", 512,
                limits::kMinimumImageDimension, limits::kMaximumImageDimension,
                height, validation_error) ||
            !read_bounded_integer(
                body, "steps", 20,
                limits::kMinimumDiffusionSteps, limits::kMaximumDiffusionSteps,
                steps, validation_error) ||
            !limits::is_valid_diffusion_request(width, height, steps)) {
            if (validation_error.empty()) {
                validation_error = "Image dimensions exceed the pixel limit";
            }
            send_error(res, validation_error);
            return;
        }
        float cfg_scale = 0.0f;
        if (!read_bounded_float(body, "cfg_scale", 7.0, 0.0, 30.0, cfg_scale, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        int64_t seed = body.value("seed", -1);

        // Use shared diffusion bridge
        auto* diffusion_bridge = get_diffusion_bridge(config_.workspace_root);

        // Check if model is loaded
        if (model.empty()) {
            auto loaded = diffusion_bridge->get_loaded_models();
            if (loaded.empty()) {
                send_error(res, "No diffusion model loaded", "no_model", 400);
                return;
            }
            model = loaded[0];
        }

        if (!diffusion_bridge->is_model_loaded(model)) {
            send_error(res, "Diffusion model not loaded: " + model, "model_not_found", 404);
            return;
        }

        ImageGenerationParams params;
        params.prompt = prompt;
        params.negative_prompt = negative_prompt;
        params.size = {width, height};
        params.steps = steps;
        params.cfg_scale = cfg_scale;
        params.seed = seed;

        auto start_time = std::chrono::high_resolution_clock::now();
        GenerationResult result = diffusion_bridge->generate_image(model, params);
        auto end_time = std::chrono::high_resolution_clock::now();

        double generation_time = std::chrono::duration<double>(end_time - start_time).count();

        if (!result.success) {
            send_error(res, result.error_message, "generation_failed", 500);
            return;
        }

        // Create images directory if it doesn't exist
        std::string images_dir = config_.workspace_root + "/images";
        fs::create_directories(images_dir);

        // Generate unique filename and save the image
        std::string image_filename = generate_completion_id() + ".png";
        std::string image_path = images_dir + "/" + image_filename;
        std::string image_url = "/api/v1/images/" + image_filename;

        // Save the first generated image
        if (!result.images.empty()) {
            bool saved = diffusion_bridge->save_image(
                result.images[0],
                result.image_size,
                image_path
            );
            if (!saved) {
                send_error(res, "Failed to save generated image", "save_failed", 500);
                return;
            }
            std::cout << "[Server] Image saved to: " << image_path << std::endl;
        }

        // Build full URL for frontend (needs base URL)
        std::string host = req.get_header_value("Host");
        if (host.empty()) host = "localhost:6930";
        std::string full_image_url = "http://" + host + image_url;

        // Return images as array (frontend expects this format)
        json images_array = json::array();
        images_array.push_back(full_image_url);

        json response = {
            {"status", "success"},
            {"images", images_array},
            {"image_url", image_url},  // Keep for backward compatibility
            {"prompt", prompt},
            {"model", model},
            {"generation_time_s", generation_time},
            {"seed", params.seed},
            {"width", width},
            {"height", height}
        };

        record_model_metrics(model, 0, generation_time * 1000.0);
        send_json(res, response.dump());

    } catch (const json::exception&) {
        send_error(res, "Invalid JSON request body");
    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
#else
    send_error(res, "Diffusion support not enabled. Build with SNAPLLM_HAS_DIFFUSION=1", "not_supported", 501);
#endif
}

void SnapLLMServer::handle_diffusion_video(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    send_error(res, "Video generation is not supported in this build", "not_supported", 501);
}

// ============================================================================
// Vision/Multimodal Endpoint
// ============================================================================

void SnapLLMServer::handle_vision_generate(const httplib::Request& req, httplib::Response& res) {
#ifdef SNAPLLM_HAS_MULTIMODAL
    total_requests_++;

    // === INFERENCE GATE ===
    if (!acquire_inference_gate(config_.timeout_seconds * 1000)) {
        total_errors_++;
        send_error(res, "Server busy - too many concurrent inference requests. Please retry.",
                   "server_busy", 503);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);
        if (!body.is_object()) {
            send_error(res, "Request body must be a JSON object");
            return;
        }

        std::string validation_error;
        std::string prompt;
        if (!read_bounded_string(
                body, "prompt", "", limits::kMaximumPromptBytes,
                false, prompt, validation_error)) {
            send_error(res, validation_error);
            return;
        }

        // Accept both 'image' (single string) and 'images' (array) from frontend
        std::vector<std::string> image_data_list;
        if (body.contains("images") && !body["images"].is_array()) {
            send_error(res, "'images' must be an array");
            return;
        }
        if (body.contains("images") && body["images"].is_array()) {
            if (!limits::is_valid_vision_image_count(body["images"].size())) {
                send_error(res, "'images' exceeds the image count limit");
                return;
            }
            for (const auto& img : body["images"]) {
                if (!img.is_string() ||
                    !limits::is_valid_base64_image_size(
                        img.get_ref<const std::string&>().size())) {
                    send_error(res, "Each image must be a bounded base64 string");
                    return;
                }
                image_data_list.push_back(img.get<std::string>());
            }
        } else if (body.contains("image") && body["image"].is_string() &&
                   limits::is_valid_base64_image_size(
                       body["image"].get_ref<const std::string&>().size())) {
            image_data_list.push_back(body["image"].get<std::string>());
        } else if (body.contains("image")) {
            send_error(res, "'image' must be a bounded base64 string");
            return;
        }

        if (image_data_list.empty()) {
            send_error(res, "Missing 'image' or 'images' (base64) in request body");
            return;
        }

        std::string model;
        if (!read_bounded_string(
                body, "model", "", limits::kMaximumStringBytes,
                true, model, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        int max_tokens = 0;
        if (!read_bounded_integer(
                body, "max_tokens", 512,
                limits::kMinimumMaxTokens, limits::kMaximumMaxTokens,
                max_tokens, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        MultimodalSamplingParams sampling;
        if (!read_bounded_float(body, "temperature", sampling.temperature, 0.0, 2.0, sampling.temperature, validation_error) ||
            !read_bounded_float(body, "top_p", sampling.top_p, 0.0, 1.0, sampling.top_p, validation_error) ||
            !read_bounded_integer(body, "top_k", sampling.top_k, 0, 1000, sampling.top_k, validation_error) ||
            !read_bounded_float(body, "repeat_penalty", sampling.repeat_penalty, 0.0, 10.0, sampling.repeat_penalty, validation_error)) {
            send_error(res, validation_error);
            return;
        }

        // Use the shared multimodal bridge
        auto* multimodal_bridge = get_multimodal_bridge();

        if (!multimodal_bridge->is_loaded()) {
            send_error(res, "No multimodal model loaded. Load a vision model first via POST /api/v1/models/load with model_type='vision' and mmproj_path", "no_model", 400);
            return;
        }

        // Decode all images and prepare inputs
        std::vector<ImageInput> images;
        for (const auto& img_b64 : image_data_list) {
            auto decoded = limits::decode_base64_strict(img_b64);
            if (!decoded || decoded->empty()) {
                send_error(res, "Failed to decode base64 image data");
                return;
            }
            std::vector<uint8_t> img_bytes = std::move(*decoded);

            int width = 0;
            int height = 0;
            int channels = 0;
            if (!stbi_info_from_memory(
                    img_bytes.data(), static_cast<int>(img_bytes.size()),
                    &width, &height, &channels) ||
                !limits::is_valid_decoded_image_dimensions(width, height)) {
                send_error(res, "Decoded image dimensions exceed the request limits");
                return;
            }

            // Use stb_image to decode PNG/JPG to RGB
            unsigned char* rgb_data = stbi_load_from_memory(
                img_bytes.data(), static_cast<int>(img_bytes.size()),
                &width, &height, &channels, 3  // Force RGB
            );

            if (!rgb_data) {
                send_error(res, "Failed to decode image format. Supported: PNG, JPG, WebP");
                return;
            }

            // Create ImageInput
            ImageInput img_input;
            img_input.width = width;
            img_input.height = height;
            img_input.data.assign(rgb_data, rgb_data + (width * height * 3));
            images.push_back(std::move(img_input));

            stbi_image_free(rgb_data);
        }

        // Add image marker to prompt if not present
        std::string full_prompt = prompt;
        std::string marker = multimodal_bridge->get_image_marker();
        if (!marker.empty() && full_prompt.find(marker) == std::string::npos) {
            // Prepend marker for each image
            for (size_t i = 0; i < images.size(); i++) {
                full_prompt = marker + "\n" + full_prompt;
            }
        }

        auto start_time = std::chrono::high_resolution_clock::now();
        MultimodalResult result = multimodal_bridge->generate(full_prompt, images, sampling, max_tokens);
        auto end_time = std::chrono::high_resolution_clock::now();

        double generation_time = std::chrono::duration<double>(end_time - start_time).count();

        if (!result.success) {
            send_error(res, result.error_message, "generation_failed", 500);
            return;
        }

        json response = {
            {"status", "success"},
            {"response", result.response},
            {"model", multimodal_bridge->get_model_info()},
            {"generation_time_s", generation_time},
            {"tokens_per_second", result.tokens_per_second}
        };

        total_tokens_ += result.tokens_generated;
        record_model_metrics(multimodal_bridge->get_model_info(),
                             static_cast<uint64_t>(result.tokens_generated),
                             generation_time * 1000.0);
        send_json(res, response.dump());

    } catch (const json::exception&) {
        send_error(res, "Invalid JSON request body");
    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
#else
    send_error(res, "Multimodal/vision support not enabled. Build with SNAPLLM_HAS_MULTIMODAL=1", "not_supported", 501);
#endif
}

// ============================================================================
// Response Utilities
// ============================================================================

void SnapLLMServer::send_json(httplib::Response& res, const std::string& json_str, int status) {
    res.status = status;
    res.set_content(json_str, MIMETYPE_JSON);
}

void SnapLLMServer::send_error(httplib::Response& res, const std::string& message,
                                const std::string& error_type, int status) {
    total_errors_++;
    json error = {
        {"error", {
            {"message", message},
            {"type", error_type},
            {"code", status}
        }}
    };
    res.status = status;
    res.set_content(error.dump(), MIMETYPE_JSON);
}

void SnapLLMServer::send_internal_error(
    httplib::Response& res,
    const char* operation,
    const std::exception& error) {
    std::cerr << "[SnapLLM Server] " << operation << " failed: "
              << error.what() << std::endl;
    send_error(res, "Internal server error", "server_error", 500);
}

std::string SnapLLMServer::generate_completion_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static const char* hex = "0123456789abcdef";

    std::string id = "chatcmpl-";
    for (int i = 0; i < 24; ++i) {
        id += hex[dis(gen)];
    }
    return id;
}

int64_t SnapLLMServer::get_timestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

int SnapLLMServer::estimate_tokens(const std::string& text) {
    // Rough estimate: 1 token ≈ 4 characters
    return static_cast<int>(text.length() / 4);
}

// ============================================================================
// WebSocket Streaming Endpoint
// ============================================================================

void SnapLLMServer::handle_websocket_upgrade(const httplib::Request& req, httplib::Response& res) {
    (void)req;
    // WebSocket streaming is not supported in the local-only build.
    // Use SSE via /v1/chat/completions with stream=true instead.
    res.status = 426; // Upgrade Required
    json response = {
        {"status", "not_supported"},
        {"message", "WebSocket streaming is not supported in this build. Use SSE via /v1/chat/completions with stream=true."},
        {"recommended", {
            {"endpoint", "/v1/chat/completions"},
            {"stream", true}
        }}
    };
    send_json(res, response.dump(), 426);
}

// ============================================================================
// Context API Endpoints (vPID L2 - KV Cache Persistence)
// ============================================================================

void SnapLLMServer::handle_context_ingest(const httplib::Request& req, httplib::Response& res) {
    // === INFERENCE GATE: Context ingestion runs inference to build KV cache ===
    if (!acquire_inference_gate(config_.timeout_seconds * 1000)) {
        total_errors_++;
        send_error(res, "Server busy - too many concurrent inference requests. Please retry.",
                   "server_busy", 503);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);

        if (!body.is_object()) {
            send_error(res, "Request body must be a JSON object");
            return;
        }

        std::string validation_error;
        std::string content;
        std::string model_id;
        std::string name;
        std::string source;
        std::string priority;
        std::string dtype;
        if (!read_bounded_string(
                body, "content", "", limits::kMaximumPromptBytes,
                false, content, validation_error) ||
            !read_bounded_string_alias(
                body, "model_id", "model", manager_->get_current_model(),
                255, false, model_id, validation_error) ||
            !read_bounded_string(
                body, "name", "", 255, true, name, validation_error) ||
            !read_bounded_string(
                body, "source", "", limits::kMaximumStringBytes,
                true, source, validation_error) ||
            !read_bounded_string(
                body, "priority", "normal", 16,
                false, priority, validation_error) ||
            !read_bounded_string(
                body, "dtype", "fp16", 8,
                false, dtype, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        if (priority != "low" && priority != "normal" && priority != "high") {
            send_error(res, "'priority' must be low, normal, or high");
            return;
        }
        if (dtype != "fp16" && dtype != "fp32" &&
            dtype != "bf16" && dtype != "int8") {
            send_error(res, "'dtype' must be fp16, fp32, bf16, or int8");
            return;
        }
        int ttl_seconds = 0;
        if (!read_bounded_integer(
                body, "ttl_seconds", 86400, 0, 31536000,
                ttl_seconds, validation_error)) {
            send_error(res, validation_error);
            return;
        }

        // Build context spec
        ContextSpec spec;
        spec.content = content;
        spec.model_id = model_id;
        spec.name = name;
        spec.source = source;
        spec.ttl_seconds = static_cast<uint32_t>(ttl_seconds);
        spec.priority = priority;

        // Configure KV cache
        if (dtype == "fp32") spec.config.dtype = KVDataType::FP32;
        else if (dtype == "bf16") spec.config.dtype = KVDataType::BF16;
        else if (dtype == "int8") spec.config.dtype = KVDataType::INT8;
        else spec.config.dtype = KVDataType::FP16;

        spec.config.compress_on_store = body.value("compress", false);

        std::cout << "[Server] Ingesting context for model '" << model_id << "'"
                  << " (" << content.size() << " chars)" << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();

        // Ingest context (synchronous for now)
        ContextHandle handle = context_manager_->ingest_sync(spec);

        auto end_time = std::chrono::high_resolution_clock::now();
        double ingest_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

        if (!handle.valid) {
            send_error(res, "Failed to ingest context", "ingest_failed", 500);
            return;
        }

        // Get metadata for response
        auto metadata = context_manager_->get_metadata(handle);

        json response = {
            {"status", "success"},
            {"context_id", handle.id},
            {"model_id", model_id},
            {"token_count", metadata ? metadata->token_count : 0},
            {"storage_size_mb", metadata ? (metadata->storage_size_bytes / (1024.0 * 1024.0)) : 0.0},
            {"tier", "hot"},
            {"ingest_time_ms", ingest_time_ms},
            {"message", "Context ingested successfully. KV cache pre-computed for reuse."}
        };

        send_json(res, response.dump(), 201);
        std::cout << "[Server] Context '" << handle.id << "' ingested in " << ingest_time_ms << "ms" << std::endl;

    } catch (const json::exception&) {
        send_error(res, "Invalid JSON request body");
    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

void SnapLLMServer::handle_context_list(const httplib::Request& req, httplib::Response& res) {
    try {
        // Get filter parameters
        std::string tier = req.get_param_value("tier");
        std::string model_id = req.get_param_value("model_id");

        std::vector<ContextHandle> handles;
        if (!tier.empty()) {
            handles = context_manager_->list_by_tier(tier);
        } else if (!model_id.empty()) {
            handles = context_manager_->list_by_model(model_id);
        } else {
            handles = context_manager_->list();
        }

        json contexts_array = json::array();
        for (const auto& handle : handles) {
            auto status = context_manager_->get_status(handle);
            auto metadata = context_manager_->get_metadata(handle);

            json ctx = {
                {"context_id", handle.id},
                {"model_id", metadata ? metadata->model_id : ""},
                {"name", metadata ? metadata->name : ""},
                {"token_count", status.token_count},
                {"memory_mb", status.memory_bytes / (1024.0 * 1024.0)},
                {"tier", status.tier},
                {"access_count", status.access_count},
                {"status", status.state == ResourceStatus::Ready ? "ready" : "loading"}
            };
            contexts_array.push_back(ctx);
        }

        json response = {
            {"status", "success"},
            {"contexts", contexts_array},
            {"count", contexts_array.size()},
            {"total_memory_mb", context_manager_->memory_usage() / (1024.0 * 1024.0)}
        };

        send_json(res, response.dump());

    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

void SnapLLMServer::handle_context_get(const httplib::Request& req, httplib::Response& res, const std::string& context_id) {
    try {
        ContextHandle handle;
        handle.id = context_id;
        handle.valid = true;

        auto status = context_manager_->get_status(handle);
        auto metadata = context_manager_->get_metadata(handle);

        if (!metadata) {
            send_error(res, "Context not found: " + context_id, "not_found", 404);
            return;
        }

        json response = {
            {"status", "success"},
            {"context", {
                {"context_id", context_id},
                {"model_id", metadata->model_id},
                {"name", metadata->name},
                {"source", metadata->source},
                {"token_count", metadata->token_count},
                {"storage_size_mb", metadata->storage_size_bytes / (1024.0 * 1024.0)},
                {"tier", metadata->tier},
                {"priority", metadata->priority},
                {"ttl_seconds", metadata->ttl_seconds},
                {"is_compressed", metadata->is_compressed},
                {"shape", {
                    {"num_layers", metadata->shape.num_layers},
                    {"num_heads", metadata->shape.num_heads},
                    {"head_dim", metadata->shape.head_dim},
                    {"sequence_length", metadata->shape.sequence_length}
                }},
                {"access_count", status.access_count},
                {"is_loaded", context_manager_->is_loaded(handle)},
                {"state", status.state == ResourceStatus::Ready ? "ready" :
                         status.state == ResourceStatus::Loading ? "loading" : "unloaded"}
            }}
        };

        send_json(res, response.dump());

    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

void SnapLLMServer::handle_context_query(const httplib::Request& req, httplib::Response& res, const std::string& context_id) {
    // === INFERENCE GATE: Context query runs inference ===
    if (!acquire_inference_gate(config_.timeout_seconds * 1000)) {
        total_errors_++;
        send_error(res, "Server busy - too many concurrent inference requests. Please retry.",
                   "server_busy", 503);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);

        std::string validation_error;
        std::string query;
        if (!read_bounded_string_alias(
                body, "query", "prompt", "",
                limits::kMaximumPromptBytes, false,
                query, validation_error)) {
            send_error(res, validation_error);
            return;
        }

        ContextHandle handle;
        handle.id = context_id;
        handle.valid = true;

        // Check if context exists
        auto metadata = context_manager_->get_metadata(handle);
        if (!metadata) {
            send_error(res, "Context not found: " + context_id, "not_found", 404);
            return;
        }

        // Build query config
        int max_tokens = 0;
        if (!read_bounded_integer(
                body, "max_tokens", 1024,
                limits::kMinimumMaxTokens, limits::kMaximumMaxTokens,
                max_tokens, validation_error)) {
            send_error(res, validation_error);
            return;
        }

        ContextQueryConfig config;
        config.max_tokens = static_cast<uint32_t>(max_tokens);
        if (!read_bounded_float(body, "temperature", 0.7, 0.0, 2.0, config.temperature, validation_error) ||
            !read_bounded_float(body, "top_p", 0.95, 0.0, 1.0, config.top_p, validation_error) ||
            !read_bounded_integer(body, "top_k", 40, 0, 1000, config.top_k, validation_error) ||
            !read_bounded_float(body, "repeat_penalty", 1.1, 0.0, 10.0, config.repeat_penalty, validation_error)) {
            send_error(res, validation_error);
            return;
        }
        config.stream = body.value("stream", false);

        std::cout << "[Server] Query with context '" << context_id
                  << "' (" << query.size() << " bytes)" << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();

        // Execute query with cached KV
        ContextQueryResult result = context_manager_->query(handle, query, config);
        if (!result.ok()) {
            const int status =
                result.status == ContextQueryResult::Status::ContextNotFound ? 404 :
                result.status == ContextQueryResult::Status::Unsupported ? 501 : 503;
            send_error(
                res,
                result.error_message.empty() ? "Context query failed" : result.error_message,
                "context_query_error",
                status);
            return;
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        double total_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

        json response = {
            {"status", "success"},
            {"context_id", context_id},
            {"response", result.text},
            {"cache_hit", result.cache_hit},
            {"usage", {
                {"context_tokens", result.usage.context_tokens},
                {"query_tokens", result.usage.query_tokens},
                {"generated_tokens", result.usage.generated_tokens},
                {"total_tokens", result.usage.context_tokens + result.usage.query_tokens + result.usage.generated_tokens}
            }},
            {"latency_ms", result.latency_ms},
            {"total_time_ms", total_time_ms},
            {"speedup", result.cache_hit ? "indexed cache lookup" : "uncached"}
        };

        send_json(res, response.dump());

        std::cout << "[Server] Query completed in " << total_time_ms << "ms"
                  << (result.cache_hit ? " (cache hit)" : "") << std::endl;

    } catch (const json::exception&) {
        send_error(res, "Invalid JSON request body");
    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

void SnapLLMServer::handle_context_delete(const httplib::Request& req, httplib::Response& res, const std::string& context_id) {
    try {
        ContextHandle handle;
        handle.id = context_id;
        handle.valid = true;

        // Check if context exists
        auto metadata = context_manager_->get_metadata(handle);
        if (!metadata) {
            send_error(res, "Context not found: " + context_id, "not_found", 404);
            return;
        }

        bool removed = context_manager_->remove(handle);

        if (removed) {
            json response = {
                {"status", "success"},
                {"message", "Context deleted: " + context_id},
                {"remaining_contexts", context_manager_->count()}
            };
            send_json(res, response.dump());
            std::cout << "[Server] Context '" << context_id << "' deleted" << std::endl;
        } else {
            send_error(res, "Failed to delete context: " + context_id, "delete_failed", 500);
        }

    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

void SnapLLMServer::handle_context_promote(const httplib::Request& req, httplib::Response& res, const std::string& context_id) {
    try {
        json body = json::parse(req.body);
        std::string target_tier = body.value("tier", "hot");

        ContextHandle handle;
        handle.id = context_id;
        handle.valid = true;

        // Check if context exists
        auto metadata = context_manager_->get_metadata(handle);
        if (!metadata) {
            send_error(res, "Context not found: " + context_id, "not_found", 404);
            return;
        }

        bool promoted = context_manager_->promote(handle, target_tier);

        if (promoted) {
            auto status = context_manager_->get_status(handle);
            json response = {
                {"status", "success"},
                {"message", "Context promoted to " + target_tier + " tier"},
                {"context_id", context_id},
                {"current_tier", status.tier},
                {"memory_mb", status.memory_bytes / (1024.0 * 1024.0)}
            };
            send_json(res, response.dump());
            std::cout << "[Server] Context '" << context_id << "' promoted to " << target_tier << " tier" << std::endl;
        } else {
            send_error(res, "Failed to promote context (invalid tier transition)", "promote_failed", 400);
        }

    } catch (const json::exception&) {
        send_error(res, "Invalid JSON request body");
    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

void SnapLLMServer::handle_context_demote(const httplib::Request& req, httplib::Response& res, const std::string& context_id) {
    try {
        json body = json::parse(req.body);
        std::string target_tier = body.value("tier", "cold");

        ContextHandle handle;
        handle.id = context_id;
        handle.valid = true;

        // Check if context exists
        auto metadata = context_manager_->get_metadata(handle);
        if (!metadata) {
            send_error(res, "Context not found: " + context_id, "not_found", 404);
            return;
        }

        bool demoted = context_manager_->demote(handle, target_tier);

        if (demoted) {
            auto status = context_manager_->get_status(handle);
            json response = {
                {"status", "success"},
                {"message", "Context demoted to " + target_tier + " tier"},
                {"context_id", context_id},
                {"current_tier", status.tier},
                {"is_loaded", context_manager_->is_loaded(handle)}
            };
            send_json(res, response.dump());
            std::cout << "[Server] Context '" << context_id << "' demoted to " << target_tier << " tier" << std::endl;
        } else {
            send_error(res, "Failed to demote context (invalid tier transition)", "demote_failed", 400);
        }

    } catch (const json::exception&) {
        send_error(res, "Invalid JSON request body");
    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

void SnapLLMServer::handle_context_stats(const httplib::Request& req, httplib::Response& res) {
    try {
        auto stats = context_manager_->get_stats();

        json response = {
            {"status", "success"},
            {"stats", {
                {"total_contexts", stats.total_contexts},
                {"hot_contexts", stats.hot_contexts},
                {"warm_contexts", stats.warm_contexts},
                {"cold_contexts", stats.cold_contexts},
                {"total_memory_mb", stats.total_memory_bytes / (1024.0 * 1024.0)},
                {"hot_memory_mb", stats.hot_memory_bytes / (1024.0 * 1024.0)},
                {"warm_memory_mb", stats.warm_memory_bytes / (1024.0 * 1024.0)},
                {"cold_memory_mb", stats.cold_memory_bytes / (1024.0 * 1024.0)},
                {"queries_total", stats.queries_total},
                {"cache_hits", stats.cache_hits},
                {"cache_misses", stats.cache_misses},
                {"hit_rate", stats.hit_rate()},
                {"avg_query_latency_ms", stats.avg_query_latency_ms}
            }},
            {"tiering_summary", {
                {"hot_tier", {
                    {"description", "GPU-ready KV caches for reuse"},
                    {"contexts", stats.hot_contexts},
                    {"memory_mb", stats.hot_memory_bytes / (1024.0 * 1024.0)}
                }},
                {"warm_tier", {
                    {"description", "CPU memory KV caches for fast reload"},
                    {"contexts", stats.warm_contexts},
                    {"memory_mb", stats.warm_memory_bytes / (1024.0 * 1024.0)}
                }},
                {"cold_tier", {
                    {"description", "Disk-persisted KV caches for capacity"},
                    {"contexts", stats.cold_contexts},
                    {"memory_mb", stats.cold_memory_bytes / (1024.0 * 1024.0)}
                }}
            }}
        };

        send_json(res, response.dump());

    } catch (const std::exception& e) {
        send_internal_error(res, "request", e);
    }
}

} // namespace snapllm
