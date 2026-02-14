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
#pragma comment(lib, "ws2_32.lib")
#endif

#include "snapllm/server.h"
#include "snapllm/websocket.h"
#include "snapllm/workspace_paths.h"
#include "snapllm/model_types.h"

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
#include <vector>

using json = nlohmann::ordered_json;

namespace snapllm {

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

static json build_persisted_config(const ServerConfig& config) {
    return json{
        {"schema_version", 1},
        {"server", {
            {"host", config.host},
            {"port", config.port},
            {"cors_enabled", config.cors_enabled},
            {"timeout_seconds", config.timeout_seconds},
            {"max_concurrent_requests", config.max_concurrent_requests}
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
    try {
        fs::path target(path);
        if (target.has_parent_path()) {
            fs::create_directories(target.parent_path());
        }
        fs::path temp_path = target;
        temp_path += ".tmp";

        std::ofstream out(temp_path, std::ios::binary);
        if (!out) {
            error = "Failed to open config file for writing";
            return false;
        }
        out << payload.dump(2);
        out.close();

        std::error_code ec;
        fs::rename(temp_path, target, ec);
        if (ec) {
            fs::remove(temp_path);
            error = "Failed to write config file: " + ec.message();
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

// Constants
static constexpr const char* MIMETYPE_JSON = "application/json; charset=utf-8";
static constexpr const char* MIMETYPE_SSE = "text/event-stream";
static constexpr const char* SNAPLLM_VERSION = "1.1.0";

// Shared DiffusionBridge instance (initialized lazily)
#ifdef SNAPLLM_HAS_DIFFUSION
static DiffusionBridge* get_diffusion_bridge(const std::string& workspace_root) {
    static std::unique_ptr<DiffusionBridge> instance;
    static std::once_flag flag;
    std::call_once(flag, [&]() {
        instance = std::make_unique<DiffusionBridge>(workspace_root + "\\diffusion");
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

    // Configure inference concurrency limits
    // HTTP-level gate: only 1 inference at a time for GPU safety
    // This prevents GPU OOM by blocking at the HTTP handler level
    // BEFORE any llama_context allocation or model switching occurs
    max_active_inferences_ = 1;  // Serialize GPU inference completely
    std::cout << "[SnapLLM] HTTP inference gate: max " << max_active_inferences_ << " concurrent" << std::endl;

    // Also configure VPIDBridge-level semaphore as a safety net
    if (auto bridge = manager_->get_bridge()) {
        bridge->set_max_concurrent_inferences(1);
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
    std::unique_lock<std::mutex> lock(inference_gate_mutex_);
    bool acquired = inference_gate_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
        return active_inference_count_ < max_active_inferences_;
    });
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
    std::cout << "    POST /api/v1/models/switch        - Switch model (<1ms)\n";
    std::cout << "    POST /api/v1/models/unload        - Unload model\n";
    std::cout << "    GET  /api/v1/models/cache/stats   - Cache statistics\n";
    std::cout << "    GET  /ws/stream                   - WebSocket streaming\n";
    std::cout << "    POST /api/v1/models/cache/clear   - Clear cache\n";
    std::cout << "    POST /api/v1/generate             - Text generation\n";
    std::cout << "    POST /api/v1/generate/batch       - Batch generation\n";
#ifdef SNAPLLM_HAS_DIFFUSION
    std::cout << "    POST /api/v1/diffusion/generate   - Image generation\n";
    std::cout << "    POST /api/v1/diffusion/video      - Video generation\n";
#endif
#ifdef SNAPLLM_HAS_MULTIMODAL
    std::cout << "    POST /api/v1/vision/generate      - Vision/multimodal\n";
#endif
    std::cout << "\n";
    std::cout << "  Context API (vPID L2 - KV Cache Persistence):\n";
    std::cout << "    POST /api/v1/contexts/ingest      - Ingest context (pre-compute KV)\n";
    std::cout << "    GET  /api/v1/contexts             - List contexts\n";
    std::cout << "    GET  /api/v1/contexts/:id         - Get context info\n";
    std::cout << "    POST /api/v1/contexts/:id/query   - Query with cached context (O(1))\n";
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
    // Pre-routing handler for CORS and OPTIONS preflight
    svr_->set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        // Add CORS headers
        if (config_.cors_enabled) {
            std::string origin = req.get_header_value("Origin");
            res.set_header("Access-Control-Allow-Origin", origin.empty() ? "*" : origin);
            res.set_header("Access-Control-Allow-Credentials", "true");
        }

        // Handle OPTIONS preflight requests
        if (req.method == "OPTIONS") {
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
            res.set_header("Access-Control-Max-Age", "86400");
            res.set_content("", "text/plain");
            return httplib::Server::HandlerResponse::Handled;
        }

        return httplib::Server::HandlerResponse::Unhandled;
    });

    // Error handler (also serves SPA fallback for Web UI)
    svr_->set_error_handler([this](const httplib::Request& req, httplib::Response& res) {
        if (req.method == "POST" && dispatch_post(req, res)) {
            return;
        }
        // SPA fallback: serve index.html for non-API GET requests (React Router)
        if (!config_.ui_dir.empty() && res.status == 404 && req.method == "GET" &&
            req.path.find("/api/") == std::string::npos &&
            req.path.find("/v1/") == std::string::npos &&
            req.path.find("/health") == std::string::npos &&
            req.path.find("/ws/") == std::string::npos) {
            auto index_path = fs::path(config_.ui_dir) / "index.html";
            if (fs::exists(index_path)) {
                std::ifstream file(index_path, std::ios::binary);
                if (file) {
                    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    res.status = 200;
                    res.set_content(content, "text/html");
                    return;
                }
            }
        }
        std::string msg = "Not found: " + req.path;
        send_error(res, msg, "not_found", 404);
    });

    // Exception handler
    svr_->set_exception_handler([this](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            send_error(res, std::string("Server error: ") + e.what(), "server_error", 500);
        } catch (...) {
            send_error(res, "Unknown server error", "server_error", 500);
        }
    });
}

// ============================================================================
// Route Setup
// ============================================================================

bool SnapLLMServer::dispatch_post(const httplib::Request& req, httplib::Response& res) {
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

    std::cout << "[Server] Registered catch-all POST handler" << std::endl;

    // =========================================================================
    // ALL GET ROUTES
    // =========================================================================

    // API info endpoint (always available)
    svr_->Get("/api", [this](const httplib::Request& req, httplib::Response& res) {
        json response = {
            {"name", "SnapLLM API"},
            {"version", SNAPLLM_VERSION},
            {"status", "running"},
            {"description", "High-performance multi-model LLM inference with <1ms switching"},
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
            {"description", "High-performance multi-model LLM inference with <1ms switching"},
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
        handle_health(req, res);
    });
    svr_->Get("/v1/health", [this](const httplib::Request& req, httplib::Response& res) {
        handle_health(req, res);
    });

    // Config and recommendations endpoints for Settings page
    svr_->Get("/api/v1/config", [this](const httplib::Request& req, httplib::Response& res) {
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
        handle_server_metrics(req, res);
    });
    svr_->Get("/v1/models", [this](const httplib::Request& req, httplib::Response& res) {
        handle_models_openai(req, res);
    });
    svr_->Get("/api/v1/models", [this](const httplib::Request& req, httplib::Response& res) {
        handle_models_extended(req, res);
    });
    svr_->Get("/api/v1/models/", [this](const httplib::Request& req, httplib::Response& res) {
        handle_models_extended(req, res);
    });
    svr_->Get("/api/v1/models/cache/stats", [this](const httplib::Request& req, httplib::Response& res) {
        handle_cache_stats(req, res);
    });
    svr_->Get("/api/v1/contexts", [this](const httplib::Request& req, httplib::Response& res) {
        handle_context_list(req, res);
    });
    svr_->Get("/api/v1/contexts/stats", [this](const httplib::Request& req, httplib::Response& res) {
        handle_context_stats(req, res);
    });
    svr_->Get(R"(/api/v1/contexts/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string context_id = req.matches[1];
        handle_context_get(req, res, context_id);
    });
    svr_->Get("/ws/stream", [this](const httplib::Request& req, httplib::Response& res) {
        handle_websocket_upgrade(req, res);
    });

#ifdef SNAPLLM_HAS_DIFFUSION
    svr_->Get(R"(/api/v1/images/([^/]+\.png))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string filename = req.matches[1];
        std::string images_dir = config_.workspace_root + "/images";
        std::string filepath = images_dir + "/" + filename;
        if (!fs::exists(filepath)) {
            send_error(res, "Image not found: " + filename, "not_found", 404);
            return;
        }
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            send_error(res, "Failed to read image", "server_error", 500);
            return;
        }
        std::vector<char> buffer(std::istreambuf_iterator<char>(file), {});
        res.set_content(buffer.data(), buffer.size(), "image/png");
    });
    svr_->Get(R"(/api/v1/videos/([^/]+)/([^/]+\.png))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string video_id = req.matches[1];
        std::string filename = req.matches[2];
        std::string videos_dir = config_.workspace_root + "/videos";
        std::string filepath = videos_dir + "/" + video_id + "/" + filename;
        if (!fs::exists(filepath)) {
            send_error(res, "Frame not found: " + filename, "not_found", 404);
            return;
        }
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            send_error(res, "Failed to read frame", "server_error", 500);
            return;
        }
        std::vector<char> buffer(std::istreambuf_iterator<char>(file), {});
        res.set_content(buffer.data(), buffer.size(), "image/png");
    });
#endif

    std::cout << "[Server] Registered all GET routes" << std::endl;

    // =========================================================================
    // DELETE ROUTES
    // =========================================================================

    svr_->Delete(R"(/api/v1/models/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        // URL decode the name (handle %20, etc.)
        std::string decoded_name;
        for (size_t i = 0; i < name.length(); ++i) {
            if (name[i] == '%' && i + 2 < name.length()) {
                int hex = std::stoi(name.substr(i + 1, 2), nullptr, 16);
                decoded_name += static_cast<char>(hex);
                i += 2;
            } else if (name[i] == '+') {
                decoded_name += ' ';
            } else {
                decoded_name += name[i];
            }
        }
        name = decoded_name;

        std::cout << "[Server] DELETE /api/v1/models/" << name << std::endl;

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
        std::string context_id = req.matches[1];
        handle_context_delete(req, res, context_id);
    });

    std::cout << "[Server] Route registration complete" << std::endl;
}

// ============================================================================
// Health Endpoint
// ============================================================================

void SnapLLMServer::handle_health(const httplib::Request&, httplib::Response& res) {
    auto models = manager_->get_loaded_models();
    std::string current = manager_->get_current_model();

    // Get ISO timestamp string
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%SZ");
    std::string timestamp_iso = ss.str();

    json response = {
        {"status", "ok"},
        {"version", SNAPLLM_VERSION},
        {"timestamp", timestamp_iso},
        {"models_loaded", static_cast<int>(models.size())},
        {"current_model", current.empty() ? nullptr : json(current)}
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
            {"device", "gpu"}
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
    if (!acquire_inference_gate(30000)) {
        total_errors_++;
        send_error(res, "Server busy - too many concurrent inference requests. Please retry.",
                   "server_busy", 503);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);

        // Extract parameters
        std::string model = body.value("model", manager_->get_current_model());
        bool stream = body.value("stream", false);
        int max_tokens = body.value("max_tokens", 2000);
        float temperature = body.value("temperature", 0.8f);
        float top_p = body.value("top_p", 0.95f);
        int top_k = body.value("top_k", 40);
        float repeat_penalty = body.value("repeat_penalty", 1.1f);

        // MCB: Option to enable L2 Context caching
        // Memory leak in KVCacheExtractor fixed - contexts now cached per model
        bool use_context_cache = body.value("use_context_cache", true);

        if (!body.contains("messages") || !body["messages"].is_array()) {
            send_error(res, "Missing 'messages' array in request body");
            return;
        }

        const auto& messages = body["messages"];
        if (messages.empty()) {
            send_error(res, "Empty 'messages' array in request body");
            return;
        }

        // Switch model if needed (vPID L1) - protected by mutex
        if (!model.empty() && model != manager_->get_current_model()) {
            std::lock_guard<std::mutex> switch_lock(model_switch_mutex_);
            if (!manager_->switch_model(model)) {
                send_error(res, "Model not loaded: " + model, "model_not_found", 404);
                return;
            }
        }

        // Check if we have an active model
        std::string current_model = manager_->get_current_model();
        if (current_model.empty()) {
            send_error(res, "No model loaded. Load a model first via POST /api/v1/models/load", "no_model", 400);
            return;
        }

        std::string completion_id = generate_completion_id();
        int64_t created = get_timestamp();

        // MCB: Separate context (history) from query (last user message)
        // Context = all messages except the last user message
        // Query = the last user message only
        std::string context_text;
        std::string query_text;
        bool found_last_user = false;

        // Find the last user message to use as query
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            if ((*it).value("role", "") == "user" && !found_last_user) {
                query_text = (*it).value("content", "");
                found_last_user = true;
            }
        }

        // Build context from all messages except the last user message
        bool skip_last_user = true;
        for (const auto& msg : messages) {
            std::string role = msg.value("role", "user");
            std::string content = msg.value("content", "");

            // Skip the last user message (that's our query)
            if (skip_last_user && role == "user" && content == query_text) {
                skip_last_user = false;  // Only skip once
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
                std::cout << "[SnapLLM MCB] Using cached context for chat, query: "
                          << query_text.substr(0, 50) << "..." << std::endl;
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
                                    return;  // Client disconnected
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
                        size_t streamed_tokens = manager_->generate_streaming(
                            full_prompt,
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
                result = manager_->generate(
                    full_prompt, static_cast<size_t>(max_tokens), &actual_tokens,
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
                {"speedup", cache_hit ? "O(1) context lookup" : "standard"},
                // Legacy x_mcb for backwards compatibility
                {"x_mcb", {
                    {"cache_hit", cache_hit},
                    {"context_id", using_cached_context ? context_handle.id : ""}
                }}
            };

            send_json(res, response.dump());
        }

    } catch (const json::exception& e) {
        send_error(res, std::string("JSON parse error: ") + e.what());
    } catch (const std::exception& e) {
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
    }
}

// ============================================================================
// Anthropic Messages API Endpoint (Claude Code Compatible)
// ============================================================================

void SnapLLMServer::handle_messages(const httplib::Request& req, httplib::Response& res) {
    total_requests_++;

    // === INFERENCE GATE: Acquire slot before ANY model/GPU operations ===
    if (!acquire_inference_gate(30000)) {
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

        // Extract parameters (Anthropic format)
        std::string model = body.value("model", manager_->get_current_model());
        bool stream = body.value("stream", false);
        int max_tokens = body.value("max_tokens", 4096);
        float temperature = body.value("temperature", 1.0f);
        float top_p = body.value("top_p", 0.999f);
        int top_k = body.value("top_k", 0);  // Anthropic uses 0 to disable

        // System prompt (Anthropic puts it at top level, not in messages)
        std::string system_prompt = body.value("system", "");

        // Extended thinking support (Anthropic feature)
        bool extended_thinking_enabled = false;
        int thinking_budget_tokens = 0;
        if (body.contains("thinking") && body["thinking"].is_object()) {
            std::string thinking_type = body["thinking"].value("type", "");
            if (thinking_type == "enabled") {
                extended_thinking_enabled = true;
                thinking_budget_tokens = body["thinking"].value("budget_tokens", 1024);
                // Ensure minimum budget
                if (thinking_budget_tokens < 100) thinking_budget_tokens = 100;
                // Cap at reasonable max
                if (thinking_budget_tokens > 32000) thinking_budget_tokens = 32000;
            }
        }

        // Extract tools if provided (Anthropic tool calling)
        std::vector<json> tools;
        bool has_tools = false;
        if (body.contains("tools") && body["tools"].is_array()) {
            has_tools = true;
            for (const auto& tool : body["tools"]) {
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

        if (body.contains("messages") && body["messages"].is_array()) {
            for (const auto& msg : body["messages"]) {
                std::string role = msg.value("role", "user");

                // Anthropic content can be string or array of content blocks
                std::string content;
                if (msg["content"].is_string()) {
                    content = msg["content"].get<std::string>();
                } else if (msg["content"].is_array()) {
                    // Array of content blocks (text, image, tool_use, tool_result, etc.)
                    for (const auto& block : msg["content"]) {
                        std::string block_type = block.value("type", "");
                        if (block_type == "text") {
                            content += block.value("text", "");
                        } else if (block_type == "image") {
                            // Image input for vision models (Anthropic format)
                            // Format: {"type": "image", "source": {"type": "base64", "media_type": "image/jpeg", "data": "..."}}
                            if (block.contains("source") && block["source"].is_object()) {
                                std::string source_type = block["source"].value("type", "");
                                if (source_type == "base64") {
                                    std::string media_type = block["source"].value("media_type", "image/jpeg");
                                    std::string image_data = block["source"].value("data", "");
                                    // Add image marker to prompt - the model manager will handle this
                                    content += "\n[IMAGE: " + media_type + ", " + std::to_string(image_data.length()) + " bytes base64]\n";
                                    // Store image data for multimodal processing
                                    // Note: Actual image processing requires SNAPLLM_HAS_MULTIMODAL
#ifdef SNAPLLM_HAS_MULTIMODAL
                                    // TODO: Pass image data to multimodal model
                                    content += "[Image data available for multimodal processing]\n";
#else
                                    content += "[Vision model not loaded - image will be described textually]\n";
#endif
                                } else if (source_type == "url") {
                                    std::string image_url = block["source"].value("url", "");
                                    content += "\n[IMAGE URL: " + image_url + "]\n";
                                }
                            }
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
                }

                if (role == "user") {
                    prompt += "\n\nHuman: " + content;
                } else if (role == "assistant") {
                    prompt += "\n\nAssistant: " + content;
                }
            }
            prompt += "\n\nAssistant:";
        } else {
            json error = {
                {"type", "error"},
                {"error", {
                    {"type", "invalid_request_error"},
                    {"message", "Missing 'messages' array in request body"}
                }}
            };
            res.status = 400;
            res.set_content(error.dump(), MIMETYPE_JSON);
            return;
        }

        // Switch model if needed - protected by mutex
        if (!model.empty() && model != manager_->get_current_model()) {
            std::lock_guard<std::mutex> switch_lock(model_switch_mutex_);
            if (!manager_->switch_model(model)) {
                // Try to find a partial match
                auto models = manager_->get_loaded_models();
                bool found = false;
                for (const auto& m : models) {
                    if (m.find(model) != std::string::npos || model.find(m) != std::string::npos) {
                        manager_->switch_model(m);
                        model = m;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    json error = {
                        {"type", "error"},
                        {"error", {
                            {"type", "not_found_error"},
                            {"message", "Model not loaded: " + model}
                        }}
                    };
                    res.status = 404;
                    res.set_content(error.dump(), MIMETYPE_JSON);
                    return;
                }
            }
        }

        // Check if we have an active model
        std::string current_model = manager_->get_current_model();
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
                    manager_->generate_streaming(
                        prompt,
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
            std::string result = manager_->generate(
                prompt, static_cast<size_t>(max_tokens), &actual_tokens,
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

    } catch (const json::exception& e) {
        json error = {
            {"type", "error"},
            {"error", {
                {"type", "invalid_request_error"},
                {"message", std::string("JSON parse error: ") + e.what()}
            }}
        };
        res.status = 400;
        res.set_content(error.dump(), MIMETYPE_JSON);
    } catch (const std::exception& e) {
        json error = {
            {"type", "error"},
            {"error", {
                {"type", "api_error"},
                {"message", std::string("Server error: ") + e.what()}
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

        auto update_string = [&](const char* key, std::string& target, std::string& live_target, bool requires_restart, bool allow_empty) {
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
                if (requires_restart) {
                    restart_required.push_back(key);
                } else {
                    live_target = value;
                }
            }
            return true;
        };

        auto update_int = [&](const char* key, int& target, int& live_target, bool requires_restart, int min_value, int max_value) {
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
                if (requires_restart) {
                    restart_required.push_back(key);
                } else {
                    live_target = value;
                }
            }
            return true;
        };

        auto update_bool = [&](const char* key, bool& target, bool& live_target, bool requires_restart) {
            if (!merged.contains(key)) return true;
            if (!merged[key].is_boolean()) {
                send_error(res, std::string("Invalid type for '") + key + "'", "invalid_request_error", 400);
                return false;
            }
            bool value = merged[key].get<bool>();
            if (target != value) {
                target = value;
                updated_fields.push_back(key);
                if (requires_restart) {
                    restart_required.push_back(key);
                } else {
                    live_target = value;
                }
            }
            return true;
        };

        if (!update_string("host", updated.host, config_.host, true, false)) return;
        if (!update_int("port", updated.port, config_.port, true, 1, 65535)) return;
        if (!update_string("workspace_root", updated.workspace_root, config_.workspace_root, true, false)) return;
        if (!update_string("default_models_path", updated.default_models_path, config_.default_models_path, false, false)) return;
        if (!update_bool("cors_enabled", updated.cors_enabled, config_.cors_enabled, true)) return;
        if (!update_int("timeout_seconds", updated.timeout_seconds, config_.timeout_seconds, true, 30, 86400)) return;
        if (!update_int("max_concurrent_requests", updated.max_concurrent_requests, config_.max_concurrent_requests, true, 1, 128)) return;
        if (!update_int("max_models", updated.max_models, config_.max_models, false, 1, 64)) return;
        if (!update_int("default_ram_budget_mb", updated.default_ram_budget_mb, config_.default_ram_budget_mb, false, 512, 1048576)) return;
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
                config_.default_strategy = normalized;
            }
        }
        if (!update_bool("enable_gpu", updated.enable_gpu, config_.enable_gpu, false)) return;

        std::string error;
        json payload = build_persisted_config(updated);
        if (!write_config_file(config_.config_path.empty() ? get_default_config_path() : config_.config_path, payload, error)) {
            send_error(res, "Failed to persist configuration: " + error, "server_error", 500);
            return;
        }

        json response = {
            {"status", "success"},
            {"updated_fields", updated_fields},
            {"restart_required", !restart_required.empty()},
            {"restart_required_fields", restart_required},
            {"config_path", config_.config_path.empty() ? get_default_config_path() : config_.config_path}
        };
        send_json(res, response.dump());
    } catch (const json::exception& e) {
        send_error(res, std::string("JSON parse error: ") + e.what(), "invalid_request_error", 400);
    } catch (const std::exception& e) {
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
    }
}

void SnapLLMServer::handle_load_model(const httplib::Request& req, httplib::Response& res) {
    try {
        json body = json::parse(req.body);

        // Support both "name"/"path" and "model_id"/"file_path" formats
        std::string name = body.value("name", body.value("model_id", ""));
        std::string path = body.value("path", body.value("file_path", ""));
        std::string model_type_str = body.value("model_type", "auto");  // auto, llm, diffusion, vision, video

        // Multi-file model paths (for SD3, FLUX, Wan2)
        std::string vae_path = body.value("vae_path", "");
        std::string t5xxl_path = body.value("t5xxl_path", "");
        std::string clip_l_path = body.value("clip_l_path", "");
        std::string clip_g_path = body.value("clip_g_path", "");
        std::string clip_vision_path = body.value("clip_vision_path", "");
        std::string high_noise_model_path = body.value("high_noise_model_path", "");

        // Vision model paths (for Gemma, Qwen, etc.)
        std::string mmproj_path = body.value("mmproj_path", body.value("vision_projector", ""));

        if (name.empty()) {
            send_error(res, "Missing 'name' or 'model_id' in request body");
            return;
        }
        if (path.empty()) {
            send_error(res, "Missing 'path' or 'file_path' in request body");
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
        } else if (model_type_str == "diffusion" || model_type_str == "sd") {
            detected_type = ModelType::IMAGE_DIFFUSION;
        } else if (model_type_str == "vision" || model_type_str == "vl") {
            detected_type = ModelType::MULTIMODAL_VL;
        } else if (model_type_str == "video") {
            detected_type = ModelType::VIDEO_DIFFUSION;
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
                mm_config.n_gpu_layers = body.value("n_gpu_layers", -1);  // -1 = all layers
                mm_config.ctx_size = body.value("ctx_size", 4096);
                mm_config.n_threads = body.value("n_threads", 4);
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
                success = manager_->load_model(name, path);
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        double load_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

        if (success) {
            // Automatically switch to the newly loaded model
            manager_->switch_model(name);
            std::cout << "[Server] Auto-switched to model: " << name << std::endl;

            bool cache_only = body.value("cache_only", false);
            json response = {
                {"status", "success"},
                {"message", "Model loaded: " + name},
                {"model", name},
                {"model_type", model_type_to_string(detected_type)},
                {"load_time_ms", load_time_ms},
                {"cache_only", cache_only},
                {"active", true}
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

    } catch (const json::exception& e) {
        send_error(res, std::string("JSON parse error: ") + e.what());
    } catch (const std::exception& e) {
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
    }
}

void SnapLLMServer::handle_switch_model(const httplib::Request& req, httplib::Response& res) {
    try {
        json body = json::parse(req.body);
        // Accept both 'model_id' (API standard) and 'name' (legacy) for compatibility
        std::string name = body.value("model_id", body.value("name", ""));

        if (name.empty()) {
            send_error(res, "Missing 'model_id' or 'name' in request body");
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

    } catch (const json::exception& e) {
        send_error(res, std::string("JSON parse error: ") + e.what());
    } catch (const std::exception& e) {
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
    }
}

void SnapLLMServer::handle_unload_model(const httplib::Request& req, httplib::Response& res) {
    try {
        json body = json::parse(req.body);
        // Accept both 'model_id' (API standard) and 'name' (legacy) for compatibility
        std::string name = body.value("model_id", body.value("name", ""));

        if (name.empty()) {
            send_error(res, "Missing 'name' in request body");
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

    } catch (const json::exception& e) {
        send_error(res, std::string("JSON parse error: ") + e.what());
    } catch (const std::exception& e) {
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
    }
}

// ============================================================================
// Folder Scanning Endpoint
// ============================================================================

void SnapLLMServer::handle_scan_folder(const httplib::Request& req, httplib::Response& res) {
    try {
        json body = json::parse(req.body);
        std::string path = body.value("path", "");

        if (path.empty()) {
            send_error(res, "Missing 'path' in request body");
            return;
        }

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
        std::vector<json> models;
        for (const auto& entry : fs::directory_iterator(path)) {
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

    } catch (const json::exception& e) {
        send_error(res, std::string("JSON parse error: ") + e.what());
    } catch (const fs::filesystem_error& e) {
        send_error(res, std::string("Filesystem error: ") + e.what(), "filesystem_error", 500);
    } catch (const std::exception& e) {
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
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
    manager_->clear_prompt_cache();

    json response = {
        {"status", "success"},
        {"message", "Cache cleared successfully"}
    };

    send_json(res, response.dump());
    std::cout << "[Server] Cache cleared" << std::endl;
}

void SnapLLMServer::handle_server_metrics(const httplib::Request&, httplib::Response& res) {
    // Calculate uptime
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();

    // Get model metrics
    auto models = manager_->get_loaded_models();
    json models_array = json::array();

    for (const auto& model_id : models) {
        double tps = 0.0;
        double avg_latency = 0.0;
        uint64_t requests = 0;
        uint64_t tokens = 0;

        {
            std::lock_guard<std::mutex> lock(model_metrics_mutex_);
            auto it = model_metrics_.find(model_id);
            if (it != model_metrics_.end()) {
                requests = it->second.requests;
                tokens = it->second.tokens_generated;
                avg_latency = (requests > 0) ? (it->second.total_latency_ms / requests) : 0.0;
                if (it->second.total_latency_ms > 0.0) {
                    tps = (tokens / (it->second.total_latency_ms / 1000.0));
                }
            }
        }

        // Fallback to workspace stats if no runtime metrics recorded
        if (requests == 0 && tokens == 0) {
            auto workspace = manager_->get_workspace(model_id);
            if (workspace) {
                const auto& stats = workspace->get_stats();
                requests = stats.total_reads.load();
                tokens = stats.bytes_read.load() / 4; // Rough estimate: 4 bytes per token
                tps = tokens > 0 && uptime > 0 ? static_cast<double>(tokens) / uptime : 0.0;
                avg_latency = requests > 0 ? 50.0 : 0.0; // Placeholder
            }
        }

        models_array.push_back({
            {"model_name", model_id},
            {"tokens_per_second", tps},
            {"avg_latency_ms", avg_latency},
            {"requests", requests},
            {"tokens_generated", tokens}
        });
    }

    json response = {
        {"status", "success"},
        {"total_requests", total_requests_.load()},
        {"total_tokens_generated", total_tokens_.load()},
        {"total_errors", total_errors_.load()},
        {"uptime_seconds", uptime},
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
    if (!acquire_inference_gate(30000)) {
        total_errors_++;
        send_error(res, "Server busy - too many concurrent inference requests. Please retry.",
                   "server_busy", 503);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);

        std::string prompt = body.value("prompt", "");
        if (prompt.empty()) {
            send_error(res, "Missing 'prompt' in request body");
            return;
        }

        std::string model = body.value("model", manager_->get_current_model());
        int max_tokens = body.value("max_tokens", 512);
        float temperature = body.value("temperature", 0.8f);
        float top_p = body.value("top_p", 0.95f);
        int top_k = body.value("top_k", 40);
        float repeat_penalty = body.value("repeat_penalty", 1.1f);

        // Switch model if needed - protected by mutex
        if (!model.empty() && model != manager_->get_current_model()) {
            std::lock_guard<std::mutex> switch_lock(model_switch_mutex_);
            if (!manager_->switch_model(model)) {
                send_error(res, "Model not loaded: " + model, "model_not_found", 404);
                return;
            }
        }

        std::string current_model = manager_->get_current_model();
        if (current_model.empty()) {
            send_error(res, "No model loaded", "no_model", 400);
            return;
        }

        auto start_time = std::chrono::high_resolution_clock::now();

        size_t actual_tokens = 0;
        std::string result = manager_->generate(
            prompt, static_cast<size_t>(max_tokens), &actual_tokens,
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

    } catch (const json::exception& e) {
        send_error(res, std::string("JSON parse error: ") + e.what());
    } catch (const std::exception& e) {
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
    }
}

void SnapLLMServer::handle_generate_batch(const httplib::Request& req, httplib::Response& res) {
    total_requests_++;

    // === INFERENCE GATE ===
    if (!acquire_inference_gate(60000)) {  // 60s timeout for batch
        total_errors_++;
        send_error(res, "Server busy - too many concurrent inference requests. Please retry.",
                   "server_busy", 503);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);

        // Parse global defaults
        std::string model = body.value("model", manager_->get_current_model());
        int default_max_tokens = body.value("max_tokens", 512);
        float default_temp = body.value("temperature", 0.8f);
        float default_top_p = body.value("top_p", 0.95f);
        int default_top_k = body.value("top_k", 40);
        float default_repeat = body.value("repeat_penalty", 1.1f);

        // Build BatchPromptItem list from either "items" (new) or "prompts" (legacy) format
        std::vector<snapllm::BatchPromptItem> items;

        if (body.contains("items") && body["items"].is_array()) {
            // New rich format with per-prompt messages and parameters
            for (const auto& item_json : body["items"]) {
                snapllm::BatchPromptItem item;

                if (item_json.contains("messages") && item_json["messages"].is_array()) {
                    for (const auto& msg : item_json["messages"]) {
                        item.messages.push_back({
                            msg.value("role", "user"),
                            msg.value("content", "")
                        });
                    }
                } else if (item_json.contains("prompt")) {
                    item.raw_prompt = item_json.value("prompt", "");
                }

                item.max_tokens = item_json.value("max_tokens", default_max_tokens);
                if (item_json.contains("temperature"))
                    item.temperature = item_json.value("temperature", 0.8f);
                if (item_json.contains("top_p"))
                    item.top_p = item_json.value("top_p", 0.95f);
                if (item_json.contains("top_k"))
                    item.top_k = item_json.value("top_k", 40);
                if (item_json.contains("repeat_penalty"))
                    item.repeat_penalty = item_json.value("repeat_penalty", 1.1f);
                if (item_json.contains("system_prompt"))
                    item.system_prompt = item_json.value("system_prompt", "");

                items.push_back(std::move(item));
            }
        } else if (body.contains("prompts") && body["prompts"].is_array()) {
            // Legacy format: simple string array
            for (const auto& p : body["prompts"]) {
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

        // Switch model if needed - protected by mutex
        if (!model.empty() && model != manager_->get_current_model()) {
            std::lock_guard<std::mutex> switch_lock(model_switch_mutex_);
            if (!manager_->switch_model(model)) {
                send_error(res, "Model not loaded: " + model, "model_not_found", 404);
                return;
            }
        }

        std::string current_model = manager_->get_current_model();
        if (current_model.empty()) {
            send_error(res, "No model loaded", "no_model", 400);
            return;
        }

        auto start_time = std::chrono::high_resolution_clock::now();

        // Use parallel batch processing
        std::vector<snapllm::BatchResult> results = manager_->generate_batch(
            items, default_temp, default_top_p, default_top_k, default_repeat);

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

    } catch (const json::exception& e) {
        send_error(res, std::string("JSON parse error: ") + e.what());
    } catch (const std::exception& e) {
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
    }
}

// ============================================================================
// Diffusion Endpoints (Image/Video Generation)
// ============================================================================

void SnapLLMServer::handle_diffusion_generate(const httplib::Request& req, httplib::Response& res) {
#ifdef SNAPLLM_HAS_DIFFUSION
    total_requests_++;

    // === INFERENCE GATE ===
    if (!acquire_inference_gate(60000)) {
        total_errors_++;
        send_error(res, "Server busy - too many concurrent inference requests. Please retry.",
                   "server_busy", 503);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);

        std::string prompt = body.value("prompt", "");
        if (prompt.empty()) {
            send_error(res, "Missing 'prompt' in request body");
            return;
        }

        std::string negative_prompt = body.value("negative_prompt", "");
        std::string model = body.value("model", "");
        int width = body.value("width", 512);
        int height = body.value("height", 512);
        int steps = body.value("steps", 20);
        float cfg_scale = body.value("cfg_scale", 7.0f);
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

    } catch (const json::exception& e) {
        send_error(res, std::string("JSON parse error: ") + e.what());
    } catch (const std::exception& e) {
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
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
    if (!acquire_inference_gate(60000)) {
        total_errors_++;
        send_error(res, "Server busy - too many concurrent inference requests. Please retry.",
                   "server_busy", 503);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);

        std::string prompt = body.value("prompt", "");
        if (prompt.empty()) {
            send_error(res, "Missing 'prompt' in request body");
            return;
        }

        // Accept both 'image' (single string) and 'images' (array) from frontend
        std::vector<std::string> image_data_list;
        if (body.contains("images") && body["images"].is_array()) {
            for (const auto& img : body["images"]) {
                if (img.is_string()) {
                    image_data_list.push_back(img.get<std::string>());
                }
            }
        } else if (body.contains("image") && body["image"].is_string()) {
            image_data_list.push_back(body["image"].get<std::string>());
        }

        if (image_data_list.empty()) {
            send_error(res, "Missing 'image' or 'images' (base64) in request body");
            return;
        }

        std::string model = body.value("model", "");
        int max_tokens = body.value("max_tokens", 512);
        MultimodalSamplingParams sampling;
        sampling.temperature = body.value("temperature", sampling.temperature);
        sampling.top_p = body.value("top_p", sampling.top_p);
        sampling.top_k = body.value("top_k", sampling.top_k);
        sampling.repeat_penalty = body.value("repeat_penalty", sampling.repeat_penalty);

        // Use the shared multimodal bridge
        auto* multimodal_bridge = get_multimodal_bridge();

        if (!multimodal_bridge->is_loaded()) {
            send_error(res, "No multimodal model loaded. Load a vision model first via POST /api/v1/models/load with model_type='vision' and mmproj_path", "no_model", 400);
            return;
        }

        // Helper lambda to decode base64
        auto base64_decode = [](const std::string& encoded) -> std::vector<uint8_t> {
            static const std::string base64_chars =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

            std::vector<uint8_t> result;
            int i = 0, j = 0, in_ = 0;
            int in_len = encoded.size();
            uint8_t char_array_4[4], char_array_3[3];

            while (in_len-- && encoded[in_] != '=' &&
                   (isalnum(encoded[in_]) || encoded[in_] == '+' || encoded[in_] == '/')) {
                char_array_4[i++] = encoded[in_++];
                if (i == 4) {
                    for (i = 0; i < 4; i++) {
                        char_array_4[i] = base64_chars.find(char_array_4[i]);
                    }
                    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
                    for (i = 0; i < 3; i++) result.push_back(char_array_3[i]);
                    i = 0;
                }
            }
            if (i) {
                for (j = i; j < 4; j++) char_array_4[j] = 0;
                for (j = 0; j < 4; j++) char_array_4[j] = base64_chars.find(char_array_4[j]);
                char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
                for (j = 0; j < i - 1; j++) result.push_back(char_array_3[j]);
            }
            return result;
        };

        // Decode all images and prepare inputs
        std::vector<ImageInput> images;
        for (const auto& img_b64 : image_data_list) {
            // Decode base64 to raw bytes
            std::vector<uint8_t> img_bytes = base64_decode(img_b64);
            if (img_bytes.empty()) {
                send_error(res, "Failed to decode base64 image data");
                return;
            }

            // Use stb_image to decode PNG/JPG to RGB
            int width, height, channels;
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

    } catch (const json::exception& e) {
        send_error(res, std::string("JSON parse error: ") + e.what());
    } catch (const std::exception& e) {
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
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
    if (!acquire_inference_gate(60000)) {
        total_errors_++;
        send_error(res, "Server busy - too many concurrent inference requests. Please retry.",
                   "server_busy", 503);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);

        // Extract required fields
        std::string content = body.value("content", "");
        std::string model_id = body.value("model_id", body.value("model", manager_->get_current_model()));

        if (content.empty()) {
            send_error(res, "Missing 'content' in request body");
            return;
        }

        if (model_id.empty()) {
            send_error(res, "Missing 'model_id' and no model loaded");
            return;
        }

        // Build context spec
        ContextSpec spec;
        spec.content = content;
        spec.model_id = model_id;
        spec.name = body.value("name", "");
        spec.source = body.value("source", "");
        spec.ttl_seconds = body.value("ttl_seconds", 86400);
        spec.priority = body.value("priority", "normal");

        // Configure KV cache
        std::string dtype = body.value("dtype", "fp16");
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
            {"message", "Context ingested successfully. KV cache pre-computed for O(1) queries."}
        };

        send_json(res, response.dump(), 201);
        std::cout << "[Server] Context '" << handle.id << "' ingested in " << ingest_time_ms << "ms" << std::endl;

    } catch (const json::exception& e) {
        send_error(res, std::string("JSON parse error: ") + e.what());
    } catch (const std::exception& e) {
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
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
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
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
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
    }
}

void SnapLLMServer::handle_context_query(const httplib::Request& req, httplib::Response& res, const std::string& context_id) {
    // === INFERENCE GATE: Context query runs inference ===
    if (!acquire_inference_gate(30000)) {
        total_errors_++;
        send_error(res, "Server busy - too many concurrent inference requests. Please retry.",
                   "server_busy", 503);
        return;
    }
    InferenceGateGuard gate_guard(true, [this]() { release_inference_gate(); });

    try {
        json body = json::parse(req.body);

        std::string query = body.value("query", body.value("prompt", ""));
        if (query.empty()) {
            send_error(res, "Missing 'query' or 'prompt' in request body");
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
        ContextQueryConfig config;
        config.max_tokens = body.value("max_tokens", 1024);
        config.temperature = body.value("temperature", 0.7f);
        config.top_p = body.value("top_p", 0.95f);
        config.top_k = body.value("top_k", 40);
        config.repeat_penalty = body.value("repeat_penalty", 1.1f);
        config.stream = body.value("stream", false);

        std::cout << "[Server] Query with context '" << context_id << "': \""
                  << query.substr(0, 50) << (query.size() > 50 ? "..." : "") << "\"" << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();

        // Execute query with cached KV
        ContextQueryResult result = context_manager_->query(handle, query, config);

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
            {"speedup", result.cache_hit ? "O(1) context lookup" : "standard O(n²)"}
        };

        send_json(res, response.dump());

        std::cout << "[Server] Query completed in " << total_time_ms << "ms"
                  << (result.cache_hit ? " (cache hit)" : "") << std::endl;

    } catch (const json::exception& e) {
        send_error(res, std::string("JSON parse error: ") + e.what());
    } catch (const std::exception& e) {
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
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
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
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

    } catch (const json::exception& e) {
        send_error(res, std::string("JSON parse error: ") + e.what());
    } catch (const std::exception& e) {
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
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

    } catch (const json::exception& e) {
        send_error(res, std::string("JSON parse error: ") + e.what());
    } catch (const std::exception& e) {
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
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
                    {"description", "GPU-ready KV caches for instant access"},
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
        send_error(res, std::string("Error: ") + e.what(), "server_error", 500);
    }
}

} // namespace snapllm
