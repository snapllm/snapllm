/**
 * @file server.h
 * @brief SnapLLM HTTP Server - OpenAI & Anthropic compatible API server
 *
 * Provides HTTP endpoints for LLM inference, eliminating the need for
 * a separate Python backend. Models stay loaded in memory for
 * ultra-fast switching (<1ms).
 *
 * Endpoints:
 *   GET  /health                      - Server health check
 *   GET  /v1/models                   - List models (OpenAI format)
 *   POST /v1/chat/completions         - Chat completion (OpenAI format)
 *   POST /v1/messages                 - Messages API (Anthropic format) - Claude Code compatible
 *   GET  /api/v1/models               - List models (extended format)
 *   POST /api/v1/models/load          - Load a model
 *   POST /api/v1/models/switch        - Switch active model
 *   POST /api/v1/models/unload        - Unload a model
 *   GET  /api/v1/models/cache/stats   - Get cache statistics
 *   POST /api/v1/models/cache/clear   - Clear cache
 *   POST /api/v1/generate             - Text generation (non-chat)
 *   POST /api/v1/generate/batch       - Batch text generation
 *   POST /api/v1/diffusion/generate   - Image generation (if enabled)
 *   POST /api/v1/diffusion/video      - Video generation (if enabled)
 *   POST /api/v1/vision/generate      - Vision/multimodal (if enabled)
 *
 * Context API (vPID L2 - KV Cache Persistence):
 *   POST /api/v1/contexts/ingest      - Ingest context (pre-compute KV cache)
 *   GET  /api/v1/contexts             - List all contexts
 *   GET  /api/v1/contexts/:id         - Get context info
 *   POST /api/v1/contexts/:id/query   - Query using cached context (O(1))
 *   DELETE /api/v1/contexts/:id       - Delete context
 *   POST /api/v1/contexts/:id/promote - Promote to hot tier
 *   POST /api/v1/contexts/:id/demote  - Demote to cold tier
 *   GET  /api/v1/contexts/stats       - Get context statistics
 *
 * Anthropic/Claude Code Integration:
 *   Set ANTHROPIC_BASE_URL=http://localhost:6930 to use with Claude Code
 *   The /v1/messages endpoint follows Anthropic's Messages API format
 */

#pragma once

#include "model_manager.h"
#include "context_manager.h"
#include <string>
#include <memory>
#include <atomic>
#include <functional>
#include <vector>
#include <unordered_map>
#include <mutex>

// Forward declarations
namespace httplib {
    class Server;
    struct Request;
    struct Response;
    class DataSink;
}

namespace snapllm {

/**
 * @brief Server configuration
 */
struct ServerConfig {
    std::string host = "127.0.0.1";         ///< Bind address (local-only by default)
    int port = 6930;                         ///< Port number (default 6930)
    std::string workspace_root = "";         ///< Model workspace (default: ~/SnapLLM_Workspace)
    std::string default_models_path = "";    ///< Default models folder (default: ~/Models or C:\Models)
    std::string config_path = "";            ///< Config file path (auto-resolved if empty)
    bool cors_enabled = true;                ///< Enable CORS for browser access
    int timeout_seconds = 600;               ///< Request timeout
    int max_concurrent_requests = 8;         ///< Max concurrent requests (future use)
    int max_models = 10;                     ///< UI default: max models allowed
    int default_ram_budget_mb = 16384;       ///< UI default RAM budget
    std::string default_strategy = "balanced"; ///< UI default strategy
    bool enable_gpu = true;                  ///< UI hint for GPU availability
};

/**
 * @brief SnapLLM HTTP Server
 *
 * Provides OpenAI-compatible REST API for LLM inference.
 * Models persist in memory across requests, enabling <1ms model switching.
 *
 * Usage:
 *   ServerConfig config;
 *   config.port = 6930;
 *   SnapLLMServer server(config);
 *
 *   // Pre-load models
 *   server.get_model_manager()->load_model("medicine", "D:\\Models\\medicine.gguf");
 *
 *   // Start server (blocking)
 *   server.start();
 */
class SnapLLMServer {
public:
    /**
     * @brief Construct server with configuration
     * @param config Server configuration
     */
    explicit SnapLLMServer(const ServerConfig& config);

    /**
     * @brief Destructor - stops server if running
     */
    ~SnapLLMServer();

    // Non-copyable
    SnapLLMServer(const SnapLLMServer&) = delete;
    SnapLLMServer& operator=(const SnapLLMServer&) = delete;

    /**
     * @brief Start the HTTP server (blocking)
     *
     * This call blocks until stop() is called or an error occurs.
     * @return true if started successfully, false on error
     */
    bool start();

    /**
     * @brief Stop the server gracefully
     *
     * Can be called from another thread or signal handler.
     */
    void stop();

    /**
     * @brief Check if server is currently running
     */
    bool is_running() const;

    /**
     * @brief Get the model manager for pre-loading models
     *
     * Use this before calling start() to pre-load models:
     *   server.get_model_manager()->load_model("name", "path.gguf");
     */
    std::shared_ptr<ModelManager> get_model_manager();

    /**
     * @brief Get server configuration
     */
    const ServerConfig& get_config() const { return config_; }

private:
    ServerConfig config_;
    std::shared_ptr<ModelManager> manager_;
    std::unique_ptr<ContextManager> context_manager_;
    WorkspacePaths workspace_paths_;
    std::unique_ptr<httplib::Server> svr_;
    std::atomic<bool> running_{false};

    // Server metrics tracking
    std::chrono::steady_clock::time_point start_time_;
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> total_tokens_{0};
    std::atomic<uint64_t> total_errors_{0};

    struct ModelRuntimeMetrics {
        uint64_t requests = 0;
        uint64_t tokens_generated = 0;
        double total_latency_ms = 0.0;
    };
    std::unordered_map<std::string, ModelRuntimeMetrics> model_metrics_;
    std::mutex model_metrics_mutex_;

    void record_model_metrics(const std::string& model_id, uint64_t tokens_generated,
                              double latency_ms, uint64_t request_count = 1);

    // Route setup
    void setup_routes();
    void setup_middleware();
    bool dispatch_post(const httplib::Request& req, httplib::Response& res);

    // === Endpoint Handlers ===

    // Health & Info
    void handle_health(const httplib::Request& req, httplib::Response& res);
    void handle_models_openai(const httplib::Request& req, httplib::Response& res);
    void handle_models_extended(const httplib::Request& req, httplib::Response& res);

    // Chat Completions (OpenAI-compatible)
    void handle_chat_completions(const httplib::Request& req, httplib::Response& res);

    // Messages API (Anthropic-compatible) - Claude Code support
    void handle_messages(const httplib::Request& req, httplib::Response& res);

    // Model Management
    void handle_config_update(const httplib::Request& req, httplib::Response& res);
    void handle_load_model(const httplib::Request& req, httplib::Response& res);
    void handle_switch_model(const httplib::Request& req, httplib::Response& res);
    void handle_unload_model(const httplib::Request& req, httplib::Response& res);
    void handle_scan_folder(const httplib::Request& req, httplib::Response& res);

    // Cache Management
    void handle_cache_stats(const httplib::Request& req, httplib::Response& res);
    void handle_cache_clear(const httplib::Request& req, httplib::Response& res);

    // Server Metrics
    void handle_server_metrics(const httplib::Request& req, httplib::Response& res);

    // Text Generation (non-chat)
    void handle_generate(const httplib::Request& req, httplib::Response& res);
    void handle_generate_batch(const httplib::Request& req, httplib::Response& res);

    // Diffusion (Image/Video Generation)
    void handle_diffusion_generate(const httplib::Request& req, httplib::Response& res);
    void handle_diffusion_video(const httplib::Request& req, httplib::Response& res);

    // Vision/Multimodal
    void handle_vision_generate(const httplib::Request& req, httplib::Response& res);

    // WebSocket Streaming
    void handle_websocket_upgrade(const httplib::Request& req, httplib::Response& res);

    // Context Management (vPID L2)
    void handle_context_ingest(const httplib::Request& req, httplib::Response& res);
    void handle_context_list(const httplib::Request& req, httplib::Response& res);
    void handle_context_get(const httplib::Request& req, httplib::Response& res, const std::string& context_id);
    void handle_context_query(const httplib::Request& req, httplib::Response& res, const std::string& context_id);
    void handle_context_delete(const httplib::Request& req, httplib::Response& res, const std::string& context_id);
    void handle_context_promote(const httplib::Request& req, httplib::Response& res, const std::string& context_id);
    void handle_context_demote(const httplib::Request& req, httplib::Response& res, const std::string& context_id);
    void handle_context_stats(const httplib::Request& req, httplib::Response& res);

    // === Response Utilities ===

    /**
     * @brief Send JSON response
     * @param res HTTP response object
     * @param data JSON data to send
     * @param status HTTP status code (default 200)
     */
    void send_json(httplib::Response& res, const std::string& json_str, int status = 200);

    /**
     * @brief Send error response in OpenAI format
     * @param res HTTP response object
     * @param message Error message
     * @param error_type Error type (invalid_request_error, server_error, etc.)
     * @param status HTTP status code
     */
    void send_error(httplib::Response& res, const std::string& message,
                    const std::string& error_type = "invalid_request_error", int status = 400);

    /**
     * @brief Generate unique completion ID
     * @return ID like "chatcmpl-abc123def456"
     */
    std::string generate_completion_id();

    /**
     * @brief Get current Unix timestamp
     */
    int64_t get_timestamp();

    /**
     * @brief Estimate token count from text (rough: 1 token ≈ 4 chars)
     */
    int estimate_tokens(const std::string& text);

    /**
     * @brief Convert messages array to prompt string
     */
    std::string messages_to_prompt(const std::string& messages_json);
};

} // namespace snapllm
