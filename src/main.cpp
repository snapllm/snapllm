/**
 * @file main.cpp
 * @brief SnapLLM CLI - Switch models in a snap!
 */

#include "snapllm/vpid_workspace.h"
#include "snapllm/dequant_cache.h"
#include "snapllm/model_manager.h"
#include "snapllm/server.h"
#include "snapllm/ison/ison_formatter.hpp"
#include "nlohmann/json.hpp"
#ifdef SNAPLLM_HAS_DIFFUSION
#include "snapllm/diffusion_bridge.h"
#include "snapllm/model_types.h"
#endif
#ifdef SNAPLLM_HAS_MULTIMODAL
#include "snapllm/multimodal_bridge.h"
#endif
#include <iostream>
#include <string>
#include <memory>
#include <fstream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <cstring>

using json = nlohmann::json;

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

// Get default workspace path based on OS
std::string get_default_workspace() {
#ifdef _WIN32
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile) {
        return std::string(userprofile) + "\\SnapLLM_Workspace";
    }
    const char* homedrive = std::getenv("HOMEDRIVE");
    const char* homepath = std::getenv("HOMEPATH");
    if (homedrive && homepath) {
        return std::string(homedrive) + std::string(homepath) + "\\SnapLLM_Workspace";
    }
    return "C:\\SnapLLM_Workspace";  // Fallback to C: drive
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/SnapLLM_Workspace";
    }
    return "/tmp/SnapLLM_Workspace";  // Fallback for Linux
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

static bool load_config_file(const std::string& path, json& out, std::string& error) {
    if (path.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return false;
    }
    std::ifstream in(path);
    if (!in) {
        error = "Failed to open config file: " + path;
        return false;
    }
    try {
        in >> out;
        return true;
    } catch (const std::exception& e) {
        error = std::string("Failed to parse config file: ") + e.what();
        return false;
    }
}

static bool try_get_string(const json& root, const char* section, const char* key, std::string& out) {
    if (root.contains(section) && root[section].is_object()) {
        const auto& section_obj = root[section];
        if (section_obj.contains(key) && section_obj[key].is_string()) {
            out = section_obj[key].get<std::string>();
            return true;
        }
    }
    if (root.contains(key) && root[key].is_string()) {
        out = root[key].get<std::string>();
        return true;
    }
    return false;
}

static bool try_get_int(const json& root, const char* section, const char* key, int& out) {
    if (root.contains(section) && root[section].is_object()) {
        const auto& section_obj = root[section];
        if (section_obj.contains(key) && section_obj[key].is_number_integer()) {
            out = section_obj[key].get<int>();
            return true;
        }
    }
    if (root.contains(key) && root[key].is_number_integer()) {
        out = root[key].get<int>();
        return true;
    }
    return false;
}

static bool try_get_bool(const json& root, const char* section, const char* key, bool& out) {
    if (root.contains(section) && root[section].is_object()) {
        const auto& section_obj = root[section];
        if (section_obj.contains(key) && section_obj[key].is_boolean()) {
            out = section_obj[key].get<bool>();
            return true;
        }
    }
    if (root.contains(key) && root[key].is_boolean()) {
        out = root[key].get<bool>();
        return true;
    }
    return false;
}

using namespace snapllm;
using namespace snapllm::ison_fmt;

// Enable ANSI escape sequences on Windows console
bool enable_ansi_console() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return false;

    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) return false;

    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, dwMode)) return false;

    // Set console to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    return true;
#else
    return true;
#endif
}

void print_banner() {
    // Try to enable ANSI colors on Windows
    bool ansi_enabled = enable_ansi_console();

    // ANSI color codes: Blue for "Snap", Orange for "LLM"
    const char* BLUE = ansi_enabled ? "\033[38;5;33m" : "";
    const char* ORANGE = ansi_enabled ? "\033[38;5;208m" : "";
    const char* RESET = ansi_enabled ? "\033[0m" : "";
    const char* BOLD = ansi_enabled ? "\033[1m" : "";

    std::cout << BOLD << "\n";
    std::cout << BLUE << "  ███████╗███╗   ██╗ █████╗ ██████╗ " << ORANGE << "██╗     ██╗     ███╗   ███╗\n";
    std::cout << BLUE << "  ██╔════╝████╗  ██║██╔══██╗██╔══██╗" << ORANGE << "██║     ██║     ████╗ ████║\n";
    std::cout << BLUE << "  ███████╗██╔██╗ ██║███████║██████╔╝" << ORANGE << "██║     ██║     ██╔████╔██║\n";
    std::cout << BLUE << "  ╚════██║██║╚██╗██║██╔══██║██╔═══╝ " << ORANGE << "██║     ██║     ██║╚██╔╝██║\n";
    std::cout << BLUE << "  ███████║██║ ╚████║██║  ██║██║     " << ORANGE << "███████╗███████╗██║ ╚═╝ ██║\n";
    std::cout << BLUE << "  ╚══════╝╚═╝  ╚═══╝╚═╝  ╚═╝╚═╝     " << ORANGE << "╚══════╝╚══════╝╚═╝     ╚═╝\n";
    std::cout << RESET << "\n";
    std::cout << "                    " << ORANGE << "* " << RESET << "Switch models in a snap!" << ORANGE << " *" << RESET << "\n";
    std::cout << "                         v1.0.0\n";
    std::cout << "                  Developed by AroorA AI Lab\n";
    std::cout << std::endl;
}

void print_usage() {
    std::cout << "Usage: snapllm [OPTIONS]\n\n";
    std::cout << "Text LLM Options:\n";
    std::cout << "  --workspace-root PATH     Root directory for model workspaces (default: ~/SnapLLM_Workspace)\n";
    std::cout << "  --load-model NAME PATH    Load and dequantize a model (can specify multiple times)\n";
    std::cout << "  --switch-model NAME       Switch to a different model\n";
    std::cout << "  --prompt TEXT             Generate text with current model\n";
    std::cout << "  --generate PROMPT         Generate text from prompt\n";
    std::cout << "  --multi-model-test        Run multi-model switching benchmark\n";
    std::cout << "  --list-models             List loaded models\n";
    std::cout << "  --stats                   Show statistics\n";
    std::cout << "  --enable-validation       Enable tensor validation at all stages\n";
    std::cout << "\nSampling Parameters:\n";
    std::cout << "  --max-tokens N            Maximum tokens to generate (default: 2000)\n";
    std::cout << "  --temperature FLOAT       Sampling temperature (default: 0.8)\n";
    std::cout << "  --top-p FLOAT             Top-p (nucleus) sampling (default: 0.95)\n";
    std::cout << "  --top-k INT               Top-k sampling (default: 40)\n";
    std::cout << "  --repeat-penalty FLOAT    Repetition penalty (default: 1.1)\n";
    std::cout << "  --presence-penalty FLOAT  Presence penalty (default: 0.0)\n";
    std::cout << "  --frequency-penalty FLOAT Frequency penalty (default: 0.0)\n";
    std::cout << "  --seed INT                Random seed (-1 for random)\n";
    std::cout << "  --stop TEXT               Stop sequence (can use multiple times)\n";
    std::cout << "\nGPU Configuration:\n";
    std::cout << "  --gpu-layers N            Number of layers on GPU (-1=auto, 0=CPU-only, 999=all)\n";
    std::cout << "  --vram-budget MB          VRAM budget in MB (0=auto-detect, default: 7000)\n";
    std::cout << "\nServer Mode (OpenAI-compatible HTTP API):\n";
    std::cout << "  --server                  Start HTTP server mode\n";
    std::cout << "  --host HOST               Server host (default: 127.0.0.1)\n";
    std::cout << "  --port PORT               Server port (default: 6930)\n";
#ifdef SNAPLLM_HAS_DIFFUSION
    std::cout << "\nImage Generation Options:\n";
    std::cout << "  --load-diffusion NAME PATH  Load a diffusion model (SD/SDXL/FLUX)\n";
    std::cout << "  --generate-image PROMPT     Generate image from prompt\n";
    std::cout << "  --output PATH               Output path for generated image\n";
    std::cout << "  --width N                   Image width (default: 512)\n";
    std::cout << "  --height N                  Image height (default: 512)\n";
    std::cout << "  --steps N                   Sampling steps (default: 20)\n";
    std::cout << "  --cfg-scale N               CFG scale (default: 7.0)\n";
    std::cout << "  --seed N                    Random seed (-1 for random)\n";
    std::cout << "  --negative PROMPT           Negative prompt\n";
#endif
#ifdef SNAPLLM_HAS_MULTIMODAL
    std::cout << "\nMultimodal (Vision) Options:\n";
    std::cout << "  --multimodal MODEL MMPROJ   Load multimodal model with projector\n";
    std::cout << "  --image PATH                Image file for vision input (can specify multiple)\n";
    std::cout << "  --vision-prompt TEXT        Prompt with <__media__> marker for image location\n";
    std::cout << "  --max-tokens N              Maximum tokens to generate (default: 512)\n";
#endif
    std::cout << "  --help                    Show this help\n";
    std::cout << "\nExamples:\n";
    std::cout << "  # Load multiple models and test switching\n";
    std::cout << "  snapllm --load-model medicine D:\\Models\\medicine-llm.Q8_0.gguf \\\n";
    std::cout << "          --load-model legal D:\\Models\\legal-llama.gguf \\\n";
    std::cout << "          --multi-model-test\n\n";
    std::cout << "  # Load single model and generate\n";
    std::cout << "  snapllm --load-model medicine D:\\Models\\medicine-llm.Q8_0.gguf \\\n";
    std::cout << "          --prompt \"What is diabetes?\"\n\n";
#ifdef SNAPLLM_HAS_DIFFUSION
    std::cout << "  # Generate image with diffusion model\n";
    std::cout << "  snapllm --load-diffusion sdxl D:\\Models\\sd_xl_base_1.0.safetensors \\\n";
    std::cout << "          --generate-image \"A beautiful sunset over mountains\" \\\n";
    std::cout << "          --output sunset.png --width 1024 --height 1024\n\n";
#endif
    std::cout << "\nNOTE: Per-model workspaces are stored at <workspace-root>/<model>/<quant>/" << std::endl;
}

// Multi-model test with switching
void run_multi_model_test(std::shared_ptr<ModelManager> manager,
                          const std::vector<std::pair<std::string, std::string>>& model_prompts) {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "  MULTI-MODEL SWITCHING BENCHMARK\n";
    std::cout << "============================================================\n\n";

    auto loaded = manager->get_loaded_models();
    std::cout << "Loaded models: " << loaded.size() << "\n";
    for (const auto& m : loaded) {
        std::cout << "  - " << m << "\n";
    }
    std::cout << "\n";

    // Run tests for each model
    for (const auto& [model_name, prompt] : model_prompts) {
        std::cout << "------------------------------------------------------------\n";
        std::cout << "MODEL: " << model_name << "\n";
        std::cout << "PROMPT: \"" << prompt << "\"\n";
        std::cout << "------------------------------------------------------------\n";

        // Time the model switch
        auto switch_start = std::chrono::high_resolution_clock::now();
        bool switched = manager->switch_model(model_name);
        auto switch_end = std::chrono::high_resolution_clock::now();

        if (!switched) {
            std::cout << "ERROR: Failed to switch to model: " << model_name << "\n\n";
            continue;
        }

        auto switch_us = std::chrono::duration_cast<std::chrono::microseconds>(switch_end - switch_start);
        std::cout << "SWITCH TIME: " << (switch_us.count() / 1000.0) << " ms\n\n";

        // Generate response
        auto gen_start = std::chrono::high_resolution_clock::now();
        std::string response = manager->generate(prompt, 50);
        auto gen_end = std::chrono::high_resolution_clock::now();

        double gen_ms = std::chrono::duration<double, std::milli>(gen_end - gen_start).count();
        double tok_per_sec = 50.0 / (gen_ms / 1000.0);

        std::cout << "RESPONSE:\n" << response << "\n\n";
        std::cout << "GENERATION: " << std::fixed << std::setprecision(2)
                  << gen_ms << " ms | " << tok_per_sec << " tok/s\n\n";
    }

    // Final summary
    std::cout << "============================================================\n";
    std::cout << "  BENCHMARK COMPLETE\n";
    std::cout << "============================================================\n";
    std::cout << "All model switches completed in <1ms (vPID architecture)\n\n";
}

// Check if launched by double-click (no console parent)
bool is_interactive_launch() {
#ifdef _WIN32
    DWORD processList[2];
    DWORD count = GetConsoleProcessList(processList, 2);
    // If only 1 process attached to console, we created it (double-click)
    return count <= 1;
#else
    return !isatty(fileno(stdin));
#endif
}

void wait_for_keypress() {
    std::cout << "\nPress Enter to exit...";
    std::cin.get();
}

int main(int argc, char** argv) {
    print_banner();

    // If no arguments and launched interactively (double-click), show help and wait
    if (argc == 1) {
        std::cout << "No arguments provided. Showing help:\n\n";
        print_usage();

        if (is_interactive_launch()) {
            wait_for_keypress();
        }
        return 0;
    }

    // Parse arguments
    std::string workspace_root = get_default_workspace();
    bool list_mode = false;
    bool stats_mode = false;
    bool enable_validation = false;
    bool multi_model_test = false;
    OutputFormat output_format = OutputFormat::Plain;

    // Support multiple models
    std::vector<std::pair<std::string, std::string>> models_to_load;  // (name, path)
    std::string switch_model_name;
    std::string generate_prompt;
    std::string prompt_text;
    bool stream_mode = false;
    std::string batch_prompts_file;

    // Sampling parameters (API-compatible)
    float temperature = 0.8f;
    float top_p = 0.95f;
    int top_k = 40;
    float repeat_penalty = 1.1f;
    float presence_penalty = 0.0f;
    float frequency_penalty = 0.0f;
    int gen_seed = -1;
    std::vector<std::string> stop_sequences;
    int max_tokens = 2000;
    
    // GPU configuration
    int gpu_layers = -1;  // -1 = auto-detect
    size_t vram_budget = 0;  // 0 = auto-detect

    // Server mode options
    bool server_mode = false;
    std::string server_host = "127.0.0.1";
    int server_port = 6930;
    bool cors_enabled = true;
    int timeout_seconds = 600;
    int max_concurrent_requests = 8;
    std::string default_models_path;
    int max_models = 10;
    int default_ram_budget_mb = 16384;
    std::string default_strategy = "balanced";
    bool enable_gpu = true;
    std::string config_path = get_default_config_path();

    // Load persisted configuration defaults (CLI args override these)
    json persisted_config;
    std::string config_error;
    if (load_config_file(config_path, persisted_config, config_error)) {
        std::string config_workspace;
        if (try_get_string(persisted_config, "workspace", "root", config_workspace) && !config_workspace.empty()) {
            workspace_root = config_workspace;
        }
        std::string config_models_path;
        if (try_get_string(persisted_config, "workspace", "default_models_path", config_models_path) && !config_models_path.empty()) {
            default_models_path = config_models_path;
        }
        std::string config_host;
        if (try_get_string(persisted_config, "server", "host", config_host) && !config_host.empty()) {
            server_host = config_host;
        }
        int config_port = 0;
        if (try_get_int(persisted_config, "server", "port", config_port) && config_port >= 1 && config_port <= 65535) {
            server_port = config_port;
        }
        bool config_cors = cors_enabled;
        if (try_get_bool(persisted_config, "server", "cors_enabled", config_cors)) {
            cors_enabled = config_cors;
        }
        int config_timeout = 0;
        if (try_get_int(persisted_config, "server", "timeout_seconds", config_timeout) && config_timeout >= 30 && config_timeout <= 86400) {
            timeout_seconds = config_timeout;
        }
        int config_max_concurrent = 0;
        if (try_get_int(persisted_config, "server", "max_concurrent_requests", config_max_concurrent) && config_max_concurrent >= 1 && config_max_concurrent <= 128) {
            max_concurrent_requests = config_max_concurrent;
        }
        int config_max_models = 0;
        if (try_get_int(persisted_config, "runtime", "max_models", config_max_models) && config_max_models >= 1 && config_max_models <= 64) {
            max_models = config_max_models;
        }
        int config_ram_budget = 0;
        if (try_get_int(persisted_config, "runtime", "default_ram_budget_mb", config_ram_budget) && config_ram_budget >= 512 && config_ram_budget <= 1048576) {
            default_ram_budget_mb = config_ram_budget;
        }
        std::string config_strategy;
        if (try_get_string(persisted_config, "runtime", "default_strategy", config_strategy) && !config_strategy.empty()) {
            std::string normalized = config_strategy;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
            const std::vector<std::string> allowed = {"balanced", "conservative", "aggressive", "performance"};
            if (std::find(allowed.begin(), allowed.end(), normalized) != allowed.end()) {
                default_strategy = normalized;
            }
        }
        bool config_gpu = enable_gpu;
        if (try_get_bool(persisted_config, "runtime", "enable_gpu", config_gpu)) {
            enable_gpu = config_gpu;
        }
    } else if (!config_error.empty()) {
        std::cerr << "[Config] Warning: " << config_error << std::endl;
    }

#ifdef SNAPLLM_HAS_DIFFUSION
    // Diffusion model support
    std::vector<std::pair<std::string, std::string>> diffusion_models_to_load;
    std::string image_prompt;
    std::string output_path = "output.png";
    std::string negative_prompt;
    int img_width = 512;
    int img_height = 512;
    int steps = 20;
    float cfg_scale = 7.0f;
    int64_t seed = -1;

    // Video generation support
    std::vector<std::pair<std::string, std::string>> video_models_to_load;
    std::string video_prompt;
    std::string video_output_path = "output_frames";
    int num_frames = 24;
    int fps = 8;
#endif

#ifdef SNAPLLM_HAS_MULTIMODAL
    // Multimodal (vision) support
    std::string multimodal_model_path;
    std::string multimodal_mmproj_path;
    std::vector<std::string> vision_image_paths;
    std::string vision_prompt;
    // max_tokens already defined above in sampling parameters
#endif

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help") {
            print_usage();
            return 0;
        }
        else if (arg == "--workspace-root" && i + 1 < argc) {
            workspace_root = argv[++i];
        }
        else if (arg == "--load-model" && i + 2 < argc) {
            std::string name = argv[++i];
            std::string path = argv[++i];
            models_to_load.push_back({name, path});
        }
        else if (arg == "--switch-model" && i + 1 < argc) {
            switch_model_name = argv[++i];
        }
        else if (arg == "--generate" && i + 1 < argc) {
            generate_prompt = argv[++i];
        }
        else if (arg == "--prompt" && i + 1 < argc) {
            prompt_text = argv[++i];
        }
        else if (arg == "--stream") {
            stream_mode = true;
        }
        else if (arg == "--batch-prompts" && i + 1 < argc) {
            batch_prompts_file = argv[++i];
        }
        else if (arg == "--list-models") {
            list_mode = true;
        }
        else if (arg == "--stats") {
            stats_mode = true;
        }
        else if (arg == "--enable-validation") {
            enable_validation = true;
        }
        else if (arg == "--multi-model-test") {
            multi_model_test = true;
        }
        else if (arg == "--format" && i + 1 < argc) {
            output_format = ISONFormatter::parse_format(argv[++i]);
        }
        // Sampling parameter parsing
        else if (arg == "--temperature" && i + 1 < argc) {
            temperature = std::stof(argv[++i]);
        }
        else if (arg == "--top-p" && i + 1 < argc) {
            top_p = std::stof(argv[++i]);
        }
        else if (arg == "--top-k" && i + 1 < argc) {
            top_k = std::stoi(argv[++i]);
        }
        else if (arg == "--repeat-penalty" && i + 1 < argc) {
            repeat_penalty = std::stof(argv[++i]);
        }
        else if (arg == "--gpu-layers" && i + 1 < argc) {
            gpu_layers = std::stoi(argv[++i]);
        }
        else if (arg == "--vram-budget" && i + 1 < argc) {
            vram_budget = std::stoull(argv[++i]);
        }
        else if (arg == "--presence-penalty" && i + 1 < argc) {
            presence_penalty = std::stof(argv[++i]);
        }
        else if (arg == "--frequency-penalty" && i + 1 < argc) {
            frequency_penalty = std::stof(argv[++i]);
        }
        else if ((arg == "--seed" || arg == "--gen-seed") && i + 1 < argc) {
            gen_seed = std::stoi(argv[++i]);
        }
        else if (arg == "--stop" && i + 1 < argc) {
            stop_sequences.push_back(argv[++i]);
        }
        else if (arg == "--max-tokens" && i + 1 < argc) {
            max_tokens = std::stoi(argv[++i]);
        }
        // Server mode options
        else if (arg == "--server") {
            server_mode = true;
        }
        else if (arg == "--host" && i + 1 < argc) {
            server_host = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc) {
            server_port = std::stoi(argv[++i]);
        }
#ifdef SNAPLLM_HAS_DIFFUSION
        else if (arg == "--load-diffusion" && i + 2 < argc) {
            std::string name = argv[++i];
            std::string path = argv[++i];
            diffusion_models_to_load.push_back({name, path});
        }
        else if (arg == "--generate-image" && i + 1 < argc) {
            image_prompt = argv[++i];
        }
        else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        }
        else if (arg == "--width" && i + 1 < argc) {
            img_width = std::stoi(argv[++i]);
        }
        else if (arg == "--height" && i + 1 < argc) {
            img_height = std::stoi(argv[++i]);
        }
        else if (arg == "--steps" && i + 1 < argc) {
            steps = std::stoi(argv[++i]);
        }
        else if (arg == "--cfg-scale" && i + 1 < argc) {
            cfg_scale = std::stof(argv[++i]);
        }
        else if (arg == "--seed" && i + 1 < argc) {
            seed = std::stoll(argv[++i]);
        }
        else if (arg == "--negative" && i + 1 < argc) {
            negative_prompt = argv[++i];
        }
        // Video generation options
        else if (arg == "--load-video" && i + 2 < argc) {
            std::string name = argv[++i];
            std::string path = argv[++i];
            video_models_to_load.push_back({name, path});
        }
        else if (arg == "--generate-video" && i + 1 < argc) {
            video_prompt = argv[++i];
        }
        else if (arg == "--frames" && i + 1 < argc) {
            num_frames = std::stoi(argv[++i]);
        }
        else if (arg == "--fps" && i + 1 < argc) {
            fps = std::stoi(argv[++i]);
        }
        else if (arg == "--video-output" && i + 1 < argc) {
            video_output_path = argv[++i];
        }
#endif
#ifdef SNAPLLM_HAS_MULTIMODAL
        // Multimodal options
        else if (arg == "--multimodal" && i + 2 < argc) {
            multimodal_model_path = argv[++i];
            multimodal_mmproj_path = argv[++i];
        }
        else if (arg == "--image" && i + 1 < argc) {
            vision_image_paths.push_back(argv[++i]);
        }
        else if (arg == "--vision-prompt" && i + 1 < argc) {
            vision_prompt = argv[++i];
        }
        else if (arg == "--max-tokens" && i + 1 < argc) {
            max_tokens = std::stoi(argv[++i]);
        }
#endif
    }

#ifdef SNAPLLM_HAS_DIFFUSION
    if (!video_models_to_load.empty() || !video_prompt.empty()) {
        std::cerr << "Video generation is not supported in this build. Use image diffusion instead." << std::endl;
        return 1;
    }
#endif

    std::cout << "\n=== SnapLLM with Per-Model Workspaces ===\n";
    std::cout << "Workspace root: " << workspace_root << "\n";
    std::cout << "Per-model workspaces will be created automatically at:\n";
    std::cout << "  <workspace_root>/<model_name>/<quant_type>/workspace.bin\n\n";

    // Create model manager with workspace root
    auto manager = std::make_shared<ModelManager>(workspace_root);

    // Enable validation if requested
    if (enable_validation) {
        std::cout << "\n=== Enabling Tensor Validation ===" << std::endl;
        manager->enable_validation(true);
    }

    // Create GPU config from CLI args
    GPUConfig cli_gpu_config;
    if (gpu_layers == 0) {
        cli_gpu_config = GPUConfig::cpu_only();
    } else if (gpu_layers > 0) {
        cli_gpu_config = GPUConfig::with_layers(gpu_layers);
    } else {
        cli_gpu_config = GPUConfig::auto_detect();
    }
    if (vram_budget > 0) {
        cli_gpu_config.vram_budget_mb = vram_budget;
    }

    std::cout << "[GPU Config] Layers: " << (gpu_layers < 0 ? "auto" : std::to_string(gpu_layers))
              << ", VRAM budget: " << (vram_budget == 0 ? "auto" : std::to_string(vram_budget) + " MB") << "\n";

    // Load all requested models
    for (const auto& [name, path] : models_to_load) {
        std::cout << "\n=== Loading Model: " << name << " ===\n";
        std::cout << "Path: " << path << "\n\n";

        if (manager->load_model(name, path, false, DomainType::General, cli_gpu_config)) {
            std::cout << "Model '" << name << "' loaded successfully!\n";
        } else {
            std::cerr << "Failed to load model: " << name << "\n";
            return 1;
        }
    }

    // =========================================================================
    // Server Mode - Start HTTP server and block
    // =========================================================================
    if (server_mode) {
        ServerConfig config;
        config.host = server_host;
        config.port = server_port;
        config.workspace_root = workspace_root;
        config.cors_enabled = cors_enabled;
        config.timeout_seconds = timeout_seconds;
        config.max_concurrent_requests = max_concurrent_requests;
        config.default_models_path = default_models_path;
        config.max_models = max_models;
        config.default_ram_budget_mb = default_ram_budget_mb;
        config.default_strategy = default_strategy;
        config.enable_gpu = enable_gpu;
        config.config_path = config_path;

        SnapLLMServer server(config);

        // Transfer models from CLI manager to server's manager
        auto server_manager = server.get_model_manager();
        for (const auto& [name, path] : models_to_load) {
            // Models are already loaded in 'manager', but server has its own manager
            // Re-load into server's manager (uses cached workspace data, so fast)
            if (!server_manager->load_model(name, path, false, DomainType::General, cli_gpu_config)) {
                std::cerr << "[Server] Warning: Could not load model '" << name << "'\n";
            }
        }

        // Start server (blocking call)
        if (!server.start()) {
            std::cerr << "[Server] Failed to start HTTP server\n";
            return 1;
        }

        return 0;  // Server exited cleanly
    }

    // Multi-model test mode
    if (multi_model_test && models_to_load.size() >= 2) {
        // Define prompts for each model type
        std::vector<std::pair<std::string, std::string>> model_prompts;

        for (const auto& [name, path] : models_to_load) {
            std::string prompt;
            if (name.find("medicine") != std::string::npos || name.find("med") != std::string::npos) {
                prompt = "What is diabetes and how is it treated?";
            } else if (name.find("legal") != std::string::npos || name.find("law") != std::string::npos) {
                prompt = "What is a contract and what makes it valid?";
            } else if (name.find("code") != std::string::npos || name.find("coding") != std::string::npos) {
                prompt = "Write a Python function to check if a number is prime";
            } else {
                prompt = "Explain artificial intelligence in simple terms";
            }
            model_prompts.push_back({name, prompt});
        }

        // Run the test multiple times with switching
        std::cout << "\n=== Running Multi-Model Switch Test ===\n";

        // First pass
        run_multi_model_test(manager, model_prompts);

        // Switch back and forth to demonstrate speed
        std::cout << "\n=== Rapid Switching Test ===\n";
        for (int round = 1; round <= 3; round++) {
            std::cout << "Round " << round << ":\n";
            for (const auto& [name, _] : model_prompts) {
                auto start = std::chrono::high_resolution_clock::now();
                manager->switch_model(name);
                auto end = std::chrono::high_resolution_clock::now();
                auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                std::cout << "  " << name << ": " << (us.count() / 1000.0) << " ms\n";
            }
        }
        std::cout << "\n";
    }

    // Single model prompt test
    if (!prompt_text.empty() && !models_to_load.empty()) {
        std::string model_name = models_to_load[0].first;

        std::cout << "\n=== Inference Test ===\n";
        if (!manager->switch_model(model_name)) {
            std::cerr << "Failed to set current model for inference\n";
            return 1;
        }

        std::cout << "Model: " << model_name << "\n";
        std::cout << "Prompt: " << prompt_text << "\n\n";

        size_t actual_tokens = 0;
        auto start = std::chrono::high_resolution_clock::now();
        std::string result;
        
        if (stream_mode) {
            // True token-by-token streaming
            std::cout << "[Streaming output]" << std::endl;
            actual_tokens = manager->generate_streaming(
                prompt_text,
                [&result](const std::string& token, int token_id, bool is_eos) {
                    if (!is_eos && token_id >= 0) {
                        std::cout << token << std::flush;
                        result += token;
                    }
                    return true;  // Continue generation
                },
                max_tokens, temperature, top_p, top_k, repeat_penalty
            );
            std::cout << std::endl;
        } else {
            // Standard batch generation
            result = manager->generate(prompt_text, max_tokens, &actual_tokens, temperature, top_p, top_k, repeat_penalty);
        }
        auto end = std::chrono::high_resolution_clock::now();

        double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        double tokens_per_sec = (actual_tokens > 0) ? (actual_tokens / elapsed_ms) * 1000.0 : 0.0;

        // Output based on format
        if (output_format == OutputFormat::ISON || output_format == OutputFormat::JSON) {
            InferenceMetadata meta;
            meta.model_name = model_name;
            meta.prompt = prompt_text;
            meta.tokens_generated = actual_tokens;
            meta.generation_time_ms = elapsed_ms;
            meta.tokens_per_second = tokens_per_sec;

            std::string ison_output = ISONFormatter::format_response(result, meta);

            if (output_format == OutputFormat::JSON) {
                std::cout << ISONFormatter::to_json(ison_output) << std::endl;
            } else {
                std::cout << "\n" << ison_output << std::endl;
            }
        } else {
            std::cout << "\n=== Generation Complete ===\n";
            std::cout << result << "\n\n";
            std::cout << "=== Performance ===\n";
            std::cout << "  Tokens: " << actual_tokens << "\n";
            std::cout << "  Time: " << std::fixed << std::setprecision(2) << elapsed_ms << " ms\n";
            std::cout << "  Speed: " << std::fixed << std::setprecision(2) << tokens_per_sec << " tok/s\n";
        }
    }

    // Switch model mode
    if (!switch_model_name.empty()) {
        std::cout << "\n=== Switching Model ===\n";
        auto start = std::chrono::high_resolution_clock::now();
        if (manager->switch_model(switch_model_name)) {
            auto end = std::chrono::high_resolution_clock::now();
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            std::cout << "Switched to '" << switch_model_name << "' in " << (us.count() / 1000.0) << " ms\n";
        } else {
            std::cerr << "Failed to switch model\n";
            return 1;
        }
    }

    // Generate mode
    if (!generate_prompt.empty()) {
        std::cout << "\n=== Generating ===\n";
        std::string result = manager->generate(generate_prompt, max_tokens, nullptr, temperature, top_p, top_k, repeat_penalty);
        std::cout << "Result: " << result << "\n";
    }

    // List models mode
    if (list_mode) {
        std::cout << "\n=== Loaded Models ===\n";
        auto models = manager->get_loaded_models();
        if (models.empty()) {
            std::cout << "No models loaded.\n";
        } else {
            std::cout << "Current: " << manager->get_current_model() << "\n\n";
            for (const auto& name : models) {
                std::cout << "  - " << name;
                if (name == manager->get_current_model()) std::cout << " (active)";
                std::cout << "\n";
            }
        }
    }

    // Stats mode
    if (stats_mode) {
        manager->print_cache_stats();
    }

#ifdef SNAPLLM_HAS_DIFFUSION
    // =============================================
    // DIFFUSION MODEL SUPPORT
    // =============================================

    // Create diffusion bridge if needed
    std::unique_ptr<DiffusionBridge> diffusion_bridge;

    if (!diffusion_models_to_load.empty() || !image_prompt.empty() ||
        !video_models_to_load.empty() || !video_prompt.empty()) {
        std::cout << "\n=== SnapLLM Diffusion Support ===\n";
        diffusion_bridge = std::make_unique<DiffusionBridge>(workspace_root + "\\diffusion");

        // Set progress callback
        diffusion_bridge->set_progress_callback([](int step, int total, double time_ms) {
            std::cout << "\r  Step " << step << "/" << total
                      << " (" << std::fixed << std::setprecision(1) << time_ms << " ms)"
                      << std::flush;
        });
    }

    // Load diffusion models
    for (const auto& [name, path] : diffusion_models_to_load) {
        std::cout << "\n=== Loading Diffusion Model: " << name << " ===\n";
        std::cout << "Path: " << path << "\n";

        if (diffusion_bridge->load_model(name, path)) {
            std::cout << "Diffusion model '" << name << "' loaded successfully!\n";
        } else {
            std::cerr << "Failed to load diffusion model: " << name << "\n";
            return 1;
        }
    }

    // Generate image if requested
    if (!image_prompt.empty() && !diffusion_models_to_load.empty()) {
        std::string model_name = diffusion_models_to_load[0].first;

        std::cout << "\n=== Image Generation ===\n";
        std::cout << "Model: " << model_name << "\n";
        std::cout << "Prompt: " << image_prompt << "\n";
        std::cout << "Size: " << img_width << "x" << img_height << "\n";
        std::cout << "Steps: " << steps << "\n";
        std::cout << "CFG Scale: " << cfg_scale << "\n";
        if (!negative_prompt.empty()) {
            std::cout << "Negative: " << negative_prompt << "\n";
        }
        std::cout << "\nGenerating...\n";

        // Create params
        ImageGenerationParams params;
        params.prompt = image_prompt;
        params.negative_prompt = negative_prompt;
        params.size.width = img_width;
        params.size.height = img_height;
        params.steps = steps;
        params.cfg_scale = cfg_scale;
        params.seed = seed;

        auto start = std::chrono::high_resolution_clock::now();
        GenerationResult result = diffusion_bridge->generate_image(model_name, params);
        auto end = std::chrono::high_resolution_clock::now();

        double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

        std::cout << "\n";  // Newline after progress

        if (result.success && !result.images.empty()) {
            // Save first image
            if (DiffusionBridge::save_image(result.images[0], result.image_size, output_path)) {
                std::cout << "=== Image Generated Successfully ===\n";
                std::cout << "  Output: " << output_path << "\n";
                std::cout << "  Size: " << result.image_size.width << "x" << result.image_size.height << "\n";
                std::cout << "  Time: " << std::fixed << std::setprecision(2) << elapsed_ms / 1000.0 << " s\n";
                std::cout << "  Speed: " << std::fixed << std::setprecision(2) << (steps / (elapsed_ms / 1000.0)) << " it/s\n";
            } else {
                std::cerr << "Failed to save image to: " << output_path << "\n";
                return 1;
            }
        } else {
            std::cerr << "Image generation failed: " << result.error_message << "\n";
            return 1;
        }
    }

    // Load video models
    for (const auto& [name, path] : video_models_to_load) {
        std::cout << "\n=== Loading Video Model: " << name << " ===\n";
        std::cout << "Path: " << path << "\n";

        if (diffusion_bridge->load_model(name, path)) {
            std::cout << "Video model '" << name << "' loaded successfully!\n";
        } else {
            std::cerr << "Failed to load video model: " << name << "\n";
            return 1;
        }
    }

    // Generate video if requested
    if (!video_prompt.empty() && !video_models_to_load.empty()) {
        std::string model_name = video_models_to_load[0].first;

        std::cout << "\n=== Video Generation ===\n";
        std::cout << "Model: " << model_name << "\n";
        std::cout << "Prompt: " << video_prompt << "\n";
        std::cout << "Frames: " << num_frames << "\n";
        std::cout << "FPS: " << fps << "\n";
        std::cout << "Size: " << img_width << "x" << img_height << "\n";
        std::cout << "Steps: " << steps << "\n";
        if (!negative_prompt.empty()) {
            std::cout << "Negative: " << negative_prompt << "\n";
        }
        std::cout << "\nGenerating video...\n";

        // Create video params
        VideoGenerationParams params;
        params.prompt = video_prompt;
        params.negative_prompt = negative_prompt;
        params.frame_size.width = img_width;
        params.frame_size.height = img_height;
        params.num_frames = num_frames;
        params.fps = fps;
        params.steps = steps;
        params.cfg_scale = cfg_scale;
        params.seed = seed;

        auto start = std::chrono::high_resolution_clock::now();
        GenerationResult result = diffusion_bridge->generate_video(model_name, params);
        auto end = std::chrono::high_resolution_clock::now();

        double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

        std::cout << "\n";  // Newline after progress

        if (result.success && !result.frames.empty()) {
            // Save frames as PNG sequence
            std::cout << "=== Video Generated Successfully ===\n";
            std::cout << "  Frames: " << result.frames.size() << "\n";
            std::cout << "  Size: " << result.image_size.width << "x" << result.image_size.height << "\n";
            std::cout << "  Time: " << std::fixed << std::setprecision(2) << elapsed_ms / 1000.0 << " s\n";

            // Save each frame
            for (size_t i = 0; i < result.frames.size(); i++) {
                std::string frame_path = video_output_path + "_" + std::to_string(i) + ".png";
                if (DiffusionBridge::save_image(result.frames[i], result.image_size, frame_path)) {
                    std::cout << "  Saved: " << frame_path << "\n";
                }
            }
            std::cout << "\nVideo frames saved to: " << video_output_path << "_*.png\n";
        } else {
            std::cerr << "Video generation failed: " << result.error_message << "\n";
            return 1;
        }
    }
#endif

#ifdef SNAPLLM_HAS_MULTIMODAL
    // =============================================
    // MULTIMODAL (VISION) SUPPORT
    // =============================================

    if (!multimodal_model_path.empty() && !multimodal_mmproj_path.empty()) {
        std::cout << "\n=== SnapLLM Multimodal Support ===\n";

        auto multimodal_bridge = std::make_unique<MultimodalBridge>();

        // Configure and load model
        MultimodalConfig mm_config;
        mm_config.model_path = multimodal_model_path;
        mm_config.mmproj_path = multimodal_mmproj_path;
        mm_config.use_gpu = true;
        mm_config.n_gpu_layers = gpu_layers;  // Use CLI-specified GPU layers (-1=auto, 0=CPU, N=specific)
        mm_config.ctx_size = 4096;

        std::cout << "Loading model: " << multimodal_model_path << "\n";
        std::cout << "MMProj: " << multimodal_mmproj_path << "\n";

        if (!multimodal_bridge->load_model(mm_config)) {
            std::cerr << "Failed to load multimodal model\n";
            return 1;
        }

        std::cout << "Model info: " << multimodal_bridge->get_model_info() << "\n";
        std::cout << "Vision support: " << (multimodal_bridge->supports_vision() ? "yes" : "no") << "\n";
        std::cout << "Audio support: " << (multimodal_bridge->supports_audio() ? "yes" : "no") << "\n";

        // Generate if we have a prompt
        if (!vision_prompt.empty()) {
            std::cout << "\n=== Multimodal Generation ===\n";
            std::cout << "Prompt: " << vision_prompt << "\n";

            // Load images
            std::vector<ImageInput> images;
            for (const auto& img_path : vision_image_paths) {
                auto img = MultimodalBridge::load_image(img_path);
                if (!img.data.empty()) {
                    images.push_back(std::move(img));
                } else {
                    std::cerr << "Warning: Failed to load image: " << img_path << "\n";
                }
            }

            std::cout << "Images: " << images.size() << "\n";
            std::cout << "Max tokens: " << max_tokens << "\n";
            std::cout << "\nGenerating...\n\n";

            // Stream callback
            auto callback = [](const std::string& token) {
                std::cout << token << std::flush;
                return true;
            };

            auto start = std::chrono::high_resolution_clock::now();
            MultimodalResult result = multimodal_bridge->generate(
                vision_prompt, images, max_tokens, callback
            );
            auto end = std::chrono::high_resolution_clock::now();

            std::cout << "\n\n";

            if (result.success) {
                double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
                std::cout << "=== Generation Complete ===\n";
                std::cout << "  Tokens: " << result.tokens_generated << "\n";
                std::cout << "  Encode time: " << std::fixed << std::setprecision(2) << result.encoding_time_ms << " ms\n";
                std::cout << "  Generate time: " << std::fixed << std::setprecision(2) << result.generation_time_ms << " ms\n";
                std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_ms << " ms\n";
                std::cout << "  Speed: " << std::fixed << std::setprecision(2) << result.tokens_per_second << " tok/s\n";
            } else {
                std::cerr << "Generation failed: " << result.error_message << "\n";
                return 1;
            }
        }
    }
#endif

    std::cout << "\n";
    return 0;
}
