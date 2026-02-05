/**
 * @file workspace_paths.h
 * @brief Workspace Path Resolution and Initialization
 *
 * Provides cross-platform path resolution for SnapLLM workspaces.
 * Supports both Model Workspace (L1) and Context Workspace (L2).
 *
 * Directory Structure:
 * SNAPLLM_HOME/
 * ├── models/                 <- Model Workspace (L1)
 * │   ├── registry.json
 * │   └── <model_id>/
 * ├── contexts/               <- Context Workspace (L2)
 * │   ├── registry.json
 * │   ├── hot/                <- GPU-ready tier
 * │   ├── warm/               <- CPU memory tier
 * │   ├── cold/               <- SSD persistent tier
 * │   └── metadata/
 * ├── runtime/
 * └── config/
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

namespace snapllm {

namespace fs = std::filesystem;

/**
 * @brief Workspace tier configuration
 */
struct TierConfig {
    std::string name;
    fs::path path;
    size_t max_size_bytes = 0;
    bool compression_enabled = false;
    int compression_level = 1;  // 1-9

    TierConfig() = default;
    TierConfig(const std::string& n, const fs::path& p, size_t max_size)
        : name(n), path(p), max_size_bytes(max_size) {}
};

/**
 * @brief Complete workspace paths structure
 */
struct WorkspacePaths {
    fs::path home;              ///< SNAPLLM_HOME root

    // Model workspace (L1)
    fs::path models;            ///< models/
    fs::path model_registry;    ///< models/registry.json

    // Context workspace (L2)
    fs::path contexts;          ///< contexts/
    fs::path contexts_hot;      ///< contexts/hot/
    fs::path contexts_warm;     ///< contexts/warm/
    fs::path contexts_cold;     ///< contexts/cold/
    fs::path contexts_metadata; ///< contexts/metadata/
    fs::path context_registry;  ///< contexts/registry.json

    // Runtime
    fs::path runtime;           ///< runtime/
    fs::path vpid_state;        ///< runtime/vpid_state.json
    fs::path locks;             ///< runtime/locks/

    // Configuration
    fs::path config;            ///< config/
    fs::path main_config;       ///< config/snapllm.json
    fs::path workspace_config;  ///< config/workspace.json

    /**
     * @brief Create WorkspacePaths from home directory
     */
    static WorkspacePaths from_home(const fs::path& home_path) {
        WorkspacePaths paths;
        paths.home = home_path;

        // Model workspace
        paths.models = home_path / "models";
        paths.model_registry = paths.models / "registry.json";

        // Context workspace
        paths.contexts = home_path / "contexts";
        paths.contexts_hot = paths.contexts / "hot";
        paths.contexts_warm = paths.contexts / "warm";
        paths.contexts_cold = paths.contexts / "cold";
        paths.contexts_metadata = paths.contexts / "metadata";
        paths.context_registry = paths.contexts / "registry.json";

        // Runtime
        paths.runtime = home_path / "runtime";
        paths.vpid_state = paths.runtime / "vpid_state.json";
        paths.locks = paths.runtime / "locks";

        // Config
        paths.config = home_path / "config";
        paths.main_config = paths.config / "snapllm.json";
        paths.workspace_config = paths.config / "workspace.json";

        return paths;
    }

    /**
     * @brief Get all directories that need to be created
     */
    std::vector<fs::path> get_required_directories() const {
        return {
            models,
            contexts,
            contexts_hot,
            contexts_warm,
            contexts_cold,
            contexts_metadata,
            runtime,
            locks,
            config
        };
    }

    /**
     * @brief Get path for a specific model's workspace
     */
    fs::path get_model_path(const std::string& model_id) const {
        return models / model_id;
    }

    /**
     * @brief Get path for a context's KV cache file
     */
    fs::path get_context_cache_path(const std::string& context_id, const std::string& tier) const {
        fs::path tier_dir;
        if (tier == "hot") tier_dir = contexts_hot;
        else if (tier == "warm") tier_dir = contexts_warm;
        else tier_dir = contexts_cold;

        return tier_dir / (context_id + ".kvc");
    }

    /**
     * @brief Get path for context metadata
     */
    fs::path get_context_metadata_path(const std::string& context_id) const {
        return contexts_metadata / (context_id + ".json");
    }
};

/**
 * @brief Workspace configuration
 */
struct WorkspaceConfig {
    // Model workspace config
    size_t max_loaded_models = 5;
    std::vector<std::string> preload_models;

    // Context workspace config
    TierConfig hot_tier{"hot", "", 16ULL * 1024 * 1024 * 1024};   // 16GB default
    TierConfig warm_tier{"warm", "", 64ULL * 1024 * 1024 * 1024}; // 64GB default
    TierConfig cold_tier{"cold", "", 500ULL * 1024 * 1024 * 1024}; // 500GB default

    // Tiering config
    uint32_t promote_threshold_accesses = 10;
    uint32_t demote_hot_to_warm_seconds = 300;
    uint32_t demote_warm_to_cold_seconds = 3600;
    uint32_t evict_cold_after_seconds = 86400;

    // Default TTL for contexts
    uint32_t default_ttl_seconds = 86400;  // 24 hours

    // Eviction policy
    std::string eviction_policy = "lru";
};

/**
 * @brief Path resolver for SnapLLM workspaces
 *
 * Resolves workspace paths with the following priority:
 * 1. SNAPLLM_HOME environment variable
 * 2. Configuration file path
 * 3. Platform-specific defaults
 */
class PathResolver {
public:
    /**
     * @brief Get the SnapLLM home directory
     * @return Resolved home path
     *
     * Resolution order:
     * 1. SNAPLLM_HOME environment variable
     * 2. Platform-specific default:
     *    - Windows: %LOCALAPPDATA%\SnapLLM
     *    - Linux: ~/.local/share/snapllm
     *    - macOS: ~/Library/Application Support/SnapLLM
     */
    static fs::path get_snapllm_home() {
        // 1. Check environment variable
        const char* env_home = std::getenv("SNAPLLM_HOME");
        if (env_home && std::strlen(env_home) > 0) {
            return fs::path(env_home);
        }

        // 2. Platform-specific defaults
        return get_platform_default();
    }

    /**
     * @brief Get workspace paths structure
     */
    static WorkspacePaths get_workspace_paths() {
        return WorkspacePaths::from_home(get_snapllm_home());
    }

    /**
     * @brief Get workspace paths from custom home
     */
    static WorkspacePaths get_workspace_paths(const fs::path& home) {
        return WorkspacePaths::from_home(home);
    }

    /**
     * @brief Check if workspace is initialized
     */
    static bool is_initialized(const fs::path& home) {
        auto paths = WorkspacePaths::from_home(home);
        return fs::exists(paths.models) &&
               fs::exists(paths.contexts) &&
               fs::exists(paths.model_registry) &&
               fs::exists(paths.context_registry);
    }

private:
    static fs::path get_platform_default() {
#ifdef _WIN32
        // Windows: %LOCALAPPDATA%\SnapLLM
        char local_app_data[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_app_data))) {
            return fs::path(local_app_data) / "SnapLLM";
        }
        return fs::path("C:\\SnapLLM");

#elif __APPLE__
        // macOS: ~/Library/Application Support/SnapLLM
        const char* home = std::getenv("HOME");
        if (home) {
            return fs::path(home) / "Library" / "Application Support" / "SnapLLM";
        }
        return fs::path("/tmp/snapllm");

#else
        // Linux: ~/.local/share/snapllm
        const char* xdg = std::getenv("XDG_DATA_HOME");
        if (xdg && std::strlen(xdg) > 0) {
            return fs::path(xdg) / "snapllm";
        }
        const char* home = std::getenv("HOME");
        if (home) {
            return fs::path(home) / ".local" / "share" / "snapllm";
        }
        return fs::path("/var/lib/snapllm");
#endif
    }
};

/**
 * @brief Workspace initializer
 *
 * Creates directory structure and initializes registry files.
 */
class WorkspaceInitializer {
public:
    /**
     * @brief Initialize workspace at default location
     * @return true if successful
     */
    static bool initialize() {
        return initialize(PathResolver::get_snapllm_home());
    }

    /**
     * @brief Initialize workspace at specified location
     * @param home Home directory path
     * @return true if successful
     */
    static bool initialize(const fs::path& home) {
        auto paths = WorkspacePaths::from_home(home);

        // Create directories
        for (const auto& dir : paths.get_required_directories()) {
            if (!fs::exists(dir)) {
                std::error_code ec;
                if (!fs::create_directories(dir, ec)) {
                    return false;
                }
            }
        }

        // Initialize model registry
        if (!fs::exists(paths.model_registry)) {
            if (!write_initial_registry(paths.model_registry, "models")) {
                return false;
            }
        }

        // Initialize context registry
        if (!fs::exists(paths.context_registry)) {
            if (!write_initial_registry(paths.context_registry, "contexts")) {
                return false;
            }
        }

        // Initialize vpid state
        if (!fs::exists(paths.vpid_state)) {
            if (!write_initial_vpid_state(paths.vpid_state)) {
                return false;
            }
        }

        return true;
    }

    /**
     * @brief Verify workspace integrity
     * @param home Home directory path
     * @return Vector of missing/invalid paths
     */
    static std::vector<fs::path> verify(const fs::path& home) {
        std::vector<fs::path> issues;
        auto paths = WorkspacePaths::from_home(home);

        for (const auto& dir : paths.get_required_directories()) {
            if (!fs::exists(dir)) {
                issues.push_back(dir);
            }
        }

        if (!fs::exists(paths.model_registry)) {
            issues.push_back(paths.model_registry);
        }
        if (!fs::exists(paths.context_registry)) {
            issues.push_back(paths.context_registry);
        }

        return issues;
    }

private:
    static bool write_initial_registry(const fs::path& path, const std::string& type) {
        std::ofstream file(path);
        if (!file) return false;

        file << "{\n";
        file << "  \"version\": \"1.0\",\n";
        file << "  \"" << type << "\": {}\n";
        file << "}\n";

        return file.good();
    }

    static bool write_initial_vpid_state(const fs::path& path) {
        std::ofstream file(path);
        if (!file) return false;

        file << "{\n";
        file << "  \"active_model\": null,\n";
        file << "  \"loaded_models\": [],\n";
        file << "  \"loaded_contexts\": [],\n";
        file << "  \"gpu_memory_used_mb\": 0,\n";
        file << "  \"cpu_memory_used_mb\": 0\n";
        file << "}\n";

        return file.good();
    }
};

} // namespace snapllm
