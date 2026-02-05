# vPID Workspace Architecture

## Unified Model & Context Workspace Design

**Document Version:** 1.0
**Date:** January 2026
**Purpose:** Define workspace structure for vPID L1 (Models) and L2 (Contexts)

---

## Table of Contents

1. [Workspace Concept](#workspace-concept)
2. [Parallel Workspace Design](#parallel-workspace-design)
3. [Directory Structure](#directory-structure)
4. [Platform-Specific Paths](#platform-specific-paths)
5. [Installation & Setup](#installation--setup)
6. [Configuration](#configuration)
7. [Memory Tiering Storage](#memory-tiering-storage)
8. [Workspace Management API](#workspace-management-api)

---

## Workspace Concept

### What is a Workspace?

A **workspace** is a dedicated storage area where vPID manages persistent resources:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    vPID WORKSPACE CONCEPT                               │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   WORKSPACE = Persistent Storage + Index + Metadata + Runtime State    │
│                                                                         │
│   ┌─────────────────────────────────────────────────────────────────┐  │
│   │                                                                 │  │
│   │   Model Workspace (L1)         Context Workspace (L2)           │  │
│   │   ────────────────────         ──────────────────────           │  │
│   │                                                                 │  │
│   │   Stores:                      Stores:                          │  │
│   │   • Model weights (.gguf)      • KV cache data (.kvc)           │  │
│   │   • Model configs              • Context metadata               │  │
│   │   • Model registry             • Context registry               │  │
│   │   • Runtime state              • Tiered cache files             │  │
│   │                                                                 │  │
│   │   Purpose:                     Purpose:                         │  │
│   │   • Fast model loading         • Fast KV cache loading          │  │
│   │   • Model persistence          • Context persistence            │  │
│   │   • <1ms switching             • O(1) query access              │  │
│   │                                                                 │  │
│   └─────────────────────────────────────────────────────────────────┘  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### Why Parallel Workspaces?

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    PARALLEL WORKSPACE RATIONALE                         │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   SAME PHILOSOPHY:                                                     │
│   ┌─────────────────────────────────────────────────────────────────┐  │
│   │                                                                 │  │
│   │   Model Workspace                Context Workspace              │  │
│   │   ───────────────                ─────────────────              │  │
│   │   "Keep models ready"    ═══     "Keep contexts ready"          │  │
│   │   "Avoid reload cost"    ═══     "Avoid recompute cost"         │  │
│   │   "Persistent storage"   ═══     "Persistent storage"           │  │
│   │   "Fast access"          ═══     "Fast access"                  │  │
│   │                                                                 │  │
│   │   Same principle → Same workspace pattern                       │  │
│   │                                                                 │  │
│   └─────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│   DIFFERENT CHARACTERISTICS:                                           │
│   ┌─────────────────────────────────────────────────────────────────┐  │
│   │                                                                 │  │
│   │   Aspect              Model Workspace    Context Workspace      │  │
│   │   ──────              ───────────────    ─────────────────      │  │
│   │   File size           Large (1-100GB)    Variable (MB-GB)       │  │
│   │   File count          Few (1-20)         Many (100s-1000s)      │  │
│   │   Update frequency    Rare               Frequent               │  │
│   │   Tiering need        Low                High                   │  │
│   │   Eviction need       Rare               Common                 │  │
│   │                                                                 │  │
│   └─────────────────────────────────────────────────────────────────┘  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Parallel Workspace Design

### Unified Workspace Structure

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    UNIFIED vPID WORKSPACE                               │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   SNAPLLM_HOME/                                                        │
│   │                                                                    │
│   ├── models/                    ← MODEL WORKSPACE (L1)                │
│   │   ├── registry.json          ← Model index                         │
│   │   ├── medicine/              ← Model: medicine                     │
│   │   │   ├── model.gguf         ← Weights                             │
│   │   │   ├── config.json        ← Model config                        │
│   │   │   └── metadata.json      ← vPID metadata                       │
│   │   ├── legal/                 ← Model: legal                        │
│   │   └── code/                  ← Model: code                         │
│   │                                                                    │
│   ├── contexts/                  ← CONTEXT WORKSPACE (L2)              │
│   │   ├── registry.json          ← Context index                       │
│   │   ├── hot/                   ← GPU-tier cache (fast access)        │
│   │   │   ├── ctx_abc123.kvc     ← KV cache file                       │
│   │   │   └── ctx_def456.kvc                                           │
│   │   ├── warm/                  ← CPU-tier cache (medium access)      │
│   │   │   ├── ctx_ghi789.kvc                                           │
│   │   │   └── ctx_jkl012.kvc                                           │
│   │   ├── cold/                  ← SSD-tier cache (persistent)         │
│   │   │   ├── ctx_mno345.kvc                                           │
│   │   │   └── ...                                                      │
│   │   └── metadata/              ← Context metadata                    │
│   │       ├── ctx_abc123.json                                          │
│   │       └── ...                                                      │
│   │                                                                    │
│   ├── runtime/                   ← RUNTIME STATE                       │
│   │   ├── vpid_state.json        ← Active vPIDs                        │
│   │   ├── memory_map.json        ← Memory allocation state             │
│   │   └── locks/                 ← Resource locks                      │
│   │                                                                    │
│   └── config/                    ← CONFIGURATION                       │
│       ├── snapllm.json           ← Main config                         │
│       └── workspace.json         ← Workspace config                    │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### Workspace Comparison Table

| Aspect | Model Workspace | Context Workspace |
|--------|-----------------|-------------------|
| **Location** | `SNAPLLM_HOME/models/` | `SNAPLLM_HOME/contexts/` |
| **Registry** | `models/registry.json` | `contexts/registry.json` |
| **Data Files** | `.gguf` (weights) | `.kvc` (KV cache) |
| **Metadata** | Per-model `metadata.json` | Per-context `metadata.json` |
| **Tiering** | Single tier (disk) | Multi-tier (hot/warm/cold) |
| **Index Key** | Model ID (vPID) | Context ID |
| **Typical Size** | 1-100 GB per model | 10 MB - 10 GB per context |
| **Persistence** | Permanent | Configurable TTL |

---

## Directory Structure

### Complete Tree View

```
SNAPLLM_HOME/
│
├── models/                              # L1: Model Workspace
│   │
│   ├── registry.json                    # Model registry index
│   │   {
│   │     "models": {
│   │       "medicine": {
│   │         "vpid": "vpid_001",
│   │         "path": "medicine/model.gguf",
│   │         "loaded": true,
│   │         "memory_mb": 7168
│   │       },
│   │       "legal": {...},
│   │       "code": {...}
│   │     }
│   │   }
│   │
│   ├── medicine/
│   │   ├── model.gguf                   # Model weights (7B = ~7GB)
│   │   ├── config.json                  # Model configuration
│   │   │   {
│   │   │     "architecture": "llama",
│   │   │     "context_length": 8192,
│   │   │     "vocab_size": 32000
│   │   │   }
│   │   └── metadata.json                # vPID metadata
│   │       {
│   │         "vpid": "vpid_001",
│   │         "created": "2026-01-15T10:00:00Z",
│   │         "last_used": "2026-01-21T14:30:00Z",
│   │         "use_count": 1547
│   │       }
│   │
│   ├── legal/
│   │   └── ...
│   │
│   └── code/
│       └── ...
│
├── contexts/                            # L2: Context Workspace
│   │
│   ├── registry.json                    # Context registry index
│   │   {
│   │     "contexts": {
│   │       "ctx_abc123": {
│   │         "model_id": "medicine",
│   │         "tier": "hot",
│   │         "token_count": 50000,
│   │         "memory_mb": 892,
│   │         "created": "2026-01-21T10:00:00Z"
│   │       },
│   │       "ctx_def456": {...}
│   │     }
│   │   }
│   │
│   ├── hot/                             # GPU-ready tier
│   │   ├── ctx_abc123.kvc               # KV cache binary
│   │   └── ctx_def456.kvc
│   │
│   ├── warm/                            # CPU memory tier
│   │   ├── ctx_ghi789.kvc
│   │   └── ctx_jkl012.kvc
│   │
│   ├── cold/                            # Persistent SSD tier
│   │   ├── ctx_mno345.kvc
│   │   ├── ctx_pqr678.kvc
│   │   └── ...
│   │
│   └── metadata/                        # Context metadata
│       ├── ctx_abc123.json
│       │   {
│       │     "context_id": "ctx_abc123",
│       │     "model_id": "medicine",
│       │     "source_hash": "sha256:...",
│       │     "token_count": 50000,
│       │     "kv_shape": {
│       │       "layers": 32,
│       │       "heads": 32,
│       │       "head_dim": 128
│       │     },
│       │     "created": "2026-01-21T10:00:00Z",
│       │     "ttl_seconds": 86400,
│       │     "access_count": 47,
│       │     "last_accessed": "2026-01-21T14:30:00Z"
│       │   }
│       └── ...
│
├── runtime/                             # Runtime state
│   │
│   ├── vpid_state.json                  # Active vPID mappings
│   │   {
│   │     "active_model": "vpid_001",
│   │     "loaded_models": ["vpid_001", "vpid_002"],
│   │     "loaded_contexts": ["ctx_abc123", "ctx_def456"],
│   │     "gpu_memory_used_mb": 12500,
│   │     "cpu_memory_used_mb": 8200
│   │   }
│   │
│   ├── memory_map.json                  # Memory allocation tracking
│   │   {
│   │     "gpu": {
│   │       "capacity_mb": 24000,
│   │       "used_mb": 12500,
│   │       "allocations": [...]
│   │     },
│   │     "cpu": {...},
│   │     "ssd": {...}
│   │   }
│   │
│   └── locks/                           # Resource locks
│       ├── model_medicine.lock
│       └── ctx_abc123.lock
│
└── config/                              # Configuration
    │
    ├── snapllm.json                     # Main configuration
    │   {
    │     "server": {
    │       "port": 6930,
    │       "host": "0.0.0.0"
    │     },
    │     "vpid": {
    │       "l1_enabled": true,
    │       "l2_enabled": true
    │     }
    │   }
    │
    └── workspace.json                   # Workspace configuration
        {
          "models": {
            "path": "models",
            "max_loaded": 5
          },
          "contexts": {
            "path": "contexts",
            "tiers": {
              "hot": {"max_mb": 16000, "path": "contexts/hot"},
              "warm": {"max_mb": 64000, "path": "contexts/warm"},
              "cold": {"max_mb": 500000, "path": "contexts/cold"}
            },
            "default_ttl_seconds": 86400,
            "eviction_policy": "lru"
          }
        }
```

---

## Platform-Specific Paths

### Default Installation Paths

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    PLATFORM DEFAULT PATHS                               │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   WINDOWS                                                              │
│   ───────                                                              │
│   Default SNAPLLM_HOME:                                                │
│     %LOCALAPPDATA%\SnapLLM\                                            │
│     → C:\Users\<username>\AppData\Local\SnapLLM\                       │
│                                                                         │
│   Alternative (program install):                                       │
│     %PROGRAMDATA%\SnapLLM\                                             │
│     → C:\ProgramData\SnapLLM\                                          │
│                                                                         │
│   Custom (user-specified):                                             │
│     D:\AI\SnapLLM\                                                     │
│     E:\Models\SnapLLM\                                                 │
│                                                                         │
│   ─────────────────────────────────────────────────────────────────    │
│                                                                         │
│   LINUX                                                                │
│   ─────                                                                │
│   Default SNAPLLM_HOME:                                                │
│     $XDG_DATA_HOME/snapllm/                                            │
│     → ~/.local/share/snapllm/                                          │
│                                                                         │
│   Alternative (system-wide):                                           │
│     /opt/snapllm/                                                      │
│     /var/lib/snapllm/                                                  │
│                                                                         │
│   ─────────────────────────────────────────────────────────────────    │
│                                                                         │
│   MACOS                                                                │
│   ─────                                                                │
│   Default SNAPLLM_HOME:                                                │
│     ~/Library/Application Support/SnapLLM/                             │
│                                                                         │
│   Alternative:                                                         │
│     /usr/local/var/snapllm/                                            │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### Path Resolution Logic

```cpp
// src/workspace/path_resolver.cpp

namespace snapllm {

class PathResolver {
public:
    static fs::path get_snapllm_home() {
        // 1. Check environment variable (highest priority)
        if (auto env = std::getenv("SNAPLLM_HOME")) {
            return fs::path(env);
        }

        // 2. Check config file
        if (auto config_path = find_config_file()) {
            auto config = load_config(*config_path);
            if (config.contains("snapllm_home")) {
                return fs::path(config["snapllm_home"]);
            }
        }

        // 3. Platform-specific defaults
        return get_platform_default();
    }

private:
    static fs::path get_platform_default() {
#ifdef _WIN32
        // Windows: %LOCALAPPDATA%\SnapLLM
        if (auto local_app_data = std::getenv("LOCALAPPDATA")) {
            return fs::path(local_app_data) / "SnapLLM";
        }
        return "C:\\SnapLLM";

#elif __APPLE__
        // macOS: ~/Library/Application Support/SnapLLM
        if (auto home = std::getenv("HOME")) {
            return fs::path(home) / "Library" / "Application Support" / "SnapLLM";
        }
        return "/tmp/snapllm";

#else
        // Linux: ~/.local/share/snapllm
        if (auto xdg = std::getenv("XDG_DATA_HOME")) {
            return fs::path(xdg) / "snapllm";
        }
        if (auto home = std::getenv("HOME")) {
            return fs::path(home) / ".local" / "share" / "snapllm";
        }
        return "/var/lib/snapllm";
#endif
    }
};

// Workspace paths
struct WorkspacePaths {
    fs::path home;           // SNAPLLM_HOME
    fs::path models;         // home/models
    fs::path contexts;       // home/contexts
    fs::path contexts_hot;   // home/contexts/hot
    fs::path contexts_warm;  // home/contexts/warm
    fs::path contexts_cold;  // home/contexts/cold
    fs::path runtime;        // home/runtime
    fs::path config;         // home/config

    static WorkspacePaths from_home(const fs::path& home) {
        return WorkspacePaths{
            .home = home,
            .models = home / "models",
            .contexts = home / "contexts",
            .contexts_hot = home / "contexts" / "hot",
            .contexts_warm = home / "contexts" / "warm",
            .contexts_cold = home / "contexts" / "cold",
            .runtime = home / "runtime",
            .config = home / "config"
        };
    }
};

}  // namespace snapllm
```

### Environment Variable Configuration

```bash
# Option 1: Set in shell profile (~/.bashrc, ~/.zshrc, PowerShell profile)

# Linux/macOS
export SNAPLLM_HOME="/path/to/snapllm"
export SNAPLLM_MODELS="$SNAPLLM_HOME/models"
export SNAPLLM_CONTEXTS="$SNAPLLM_HOME/contexts"

# Windows (PowerShell)
$env:SNAPLLM_HOME = "D:\AI\SnapLLM"
$env:SNAPLLM_MODELS = "$env:SNAPLLM_HOME\models"
$env:SNAPLLM_CONTEXTS = "$env:SNAPLLM_HOME\contexts"

# Windows (CMD)
set SNAPLLM_HOME=D:\AI\SnapLLM
set SNAPLLM_MODELS=%SNAPLLM_HOME%\models
set SNAPLLM_CONTEXTS=%SNAPLLM_HOME%\contexts
```

---

## Installation & Setup

### Installer Script (Cross-Platform)

```bash
#!/bin/bash
# install_workspace.sh - Sets up SnapLLM workspace

set -e

# Determine SNAPLLM_HOME
if [ -z "$SNAPLLM_HOME" ]; then
    if [ "$(uname)" = "Darwin" ]; then
        SNAPLLM_HOME="$HOME/Library/Application Support/SnapLLM"
    elif [ "$(uname)" = "Linux" ]; then
        SNAPLLM_HOME="${XDG_DATA_HOME:-$HOME/.local/share}/snapllm"
    else
        SNAPLLM_HOME="$HOME/SnapLLM"
    fi
fi

echo "Setting up SnapLLM workspace at: $SNAPLLM_HOME"

# Create directory structure
mkdir -p "$SNAPLLM_HOME"/{models,contexts/{hot,warm,cold,metadata},runtime/locks,config}

# Initialize registry files
cat > "$SNAPLLM_HOME/models/registry.json" << 'EOF'
{
  "version": "1.0",
  "models": {}
}
EOF

cat > "$SNAPLLM_HOME/contexts/registry.json" << 'EOF'
{
  "version": "1.0",
  "contexts": {}
}
EOF

# Initialize runtime state
cat > "$SNAPLLM_HOME/runtime/vpid_state.json" << 'EOF'
{
  "active_model": null,
  "loaded_models": [],
  "loaded_contexts": [],
  "gpu_memory_used_mb": 0,
  "cpu_memory_used_mb": 0
}
EOF

# Initialize workspace config
cat > "$SNAPLLM_HOME/config/workspace.json" << 'EOF'
{
  "version": "1.0",
  "models": {
    "path": "models",
    "max_loaded": 5
  },
  "contexts": {
    "path": "contexts",
    "tiers": {
      "hot": {
        "max_mb": 16000,
        "path": "contexts/hot"
      },
      "warm": {
        "max_mb": 64000,
        "path": "contexts/warm"
      },
      "cold": {
        "max_mb": 500000,
        "path": "contexts/cold"
      }
    },
    "default_ttl_seconds": 86400,
    "eviction_policy": "lru"
  }
}
EOF

# Initialize main config
cat > "$SNAPLLM_HOME/config/snapllm.json" << 'EOF'
{
  "version": "1.0",
  "server": {
    "host": "0.0.0.0",
    "port": 6930
  },
  "vpid": {
    "l1_enabled": true,
    "l2_enabled": true
  },
  "memory": {
    "gpu_budget_mb": 20000,
    "cpu_budget_mb": 64000
  }
}
EOF

echo "Workspace initialized successfully!"
echo ""
echo "Directory structure:"
find "$SNAPLLM_HOME" -type d | head -20

echo ""
echo "Add to your shell profile:"
echo "  export SNAPLLM_HOME=\"$SNAPLLM_HOME\""
```

### Windows Installer (PowerShell)

```powershell
# install_workspace.ps1 - Sets up SnapLLM workspace on Windows

param(
    [string]$InstallPath = $null
)

# Determine SNAPLLM_HOME
if (-not $InstallPath) {
    if ($env:SNAPLLM_HOME) {
        $InstallPath = $env:SNAPLLM_HOME
    } else {
        $InstallPath = Join-Path $env:LOCALAPPDATA "SnapLLM"
    }
}

Write-Host "Setting up SnapLLM workspace at: $InstallPath" -ForegroundColor Cyan

# Create directory structure
$directories = @(
    "$InstallPath\models",
    "$InstallPath\contexts\hot",
    "$InstallPath\contexts\warm",
    "$InstallPath\contexts\cold",
    "$InstallPath\contexts\metadata",
    "$InstallPath\runtime\locks",
    "$InstallPath\config"
)

foreach ($dir in $directories) {
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
        Write-Host "  Created: $dir" -ForegroundColor Green
    }
}

# Initialize registry files
$modelRegistry = @{
    version = "1.0"
    models = @{}
} | ConvertTo-Json -Depth 10
Set-Content -Path "$InstallPath\models\registry.json" -Value $modelRegistry

$contextRegistry = @{
    version = "1.0"
    contexts = @{}
} | ConvertTo-Json -Depth 10
Set-Content -Path "$InstallPath\contexts\registry.json" -Value $contextRegistry

# Initialize workspace config
$workspaceConfig = @{
    version = "1.0"
    models = @{
        path = "models"
        max_loaded = 5
    }
    contexts = @{
        path = "contexts"
        tiers = @{
            hot = @{ max_mb = 16000; path = "contexts\hot" }
            warm = @{ max_mb = 64000; path = "contexts\warm" }
            cold = @{ max_mb = 500000; path = "contexts\cold" }
        }
        default_ttl_seconds = 86400
        eviction_policy = "lru"
    }
} | ConvertTo-Json -Depth 10
Set-Content -Path "$InstallPath\config\workspace.json" -Value $workspaceConfig

# Set environment variable (user scope)
[Environment]::SetEnvironmentVariable("SNAPLLM_HOME", $InstallPath, "User")
$env:SNAPLLM_HOME = $InstallPath

Write-Host ""
Write-Host "Workspace initialized successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "Environment variable SNAPLLM_HOME set to: $InstallPath" -ForegroundColor Yellow
Write-Host "Restart your terminal to apply changes." -ForegroundColor Yellow
```

### First-Run Initialization (C++)

```cpp
// src/workspace/workspace_initializer.cpp

namespace snapllm {

class WorkspaceInitializer {
public:
    static bool initialize(const fs::path& home) {
        WorkspacePaths paths = WorkspacePaths::from_home(home);

        // Create all directories
        std::vector<fs::path> dirs = {
            paths.models,
            paths.contexts,
            paths.contexts_hot,
            paths.contexts_warm,
            paths.contexts_cold,
            paths.contexts / "metadata",
            paths.runtime,
            paths.runtime / "locks",
            paths.config
        };

        for (const auto& dir : dirs) {
            if (!fs::exists(dir)) {
                fs::create_directories(dir);
                LOG_INFO("Created directory: {}", dir.string());
            }
        }

        // Initialize registries
        initialize_model_registry(paths.models / "registry.json");
        initialize_context_registry(paths.contexts / "registry.json");

        // Initialize runtime state
        initialize_runtime_state(paths.runtime / "vpid_state.json");

        // Initialize configs if not exist
        initialize_config_if_missing(paths.config / "workspace.json");
        initialize_config_if_missing(paths.config / "snapllm.json");

        return true;
    }

    static bool verify_workspace(const fs::path& home) {
        WorkspacePaths paths = WorkspacePaths::from_home(home);

        std::vector<fs::path> required = {
            paths.models,
            paths.contexts,
            paths.models / "registry.json",
            paths.contexts / "registry.json"
        };

        for (const auto& path : required) {
            if (!fs::exists(path)) {
                LOG_ERROR("Missing required path: {}", path.string());
                return false;
            }
        }

        return true;
    }

private:
    static void initialize_model_registry(const fs::path& path) {
        if (fs::exists(path)) return;

        json registry = {
            {"version", "1.0"},
            {"models", json::object()}
        };

        std::ofstream file(path);
        file << registry.dump(2);
    }

    static void initialize_context_registry(const fs::path& path) {
        if (fs::exists(path)) return;

        json registry = {
            {"version", "1.0"},
            {"contexts", json::object()}
        };

        std::ofstream file(path);
        file << registry.dump(2);
    }
};

}  // namespace snapllm
```

---

## Configuration

### workspace.json - Full Configuration

```json
{
  "version": "1.0",

  "models": {
    "path": "models",
    "max_loaded": 5,
    "preload": ["medicine"],
    "auto_unload_timeout_seconds": 3600
  },

  "contexts": {
    "path": "contexts",

    "tiers": {
      "hot": {
        "description": "GPU-ready, fastest access",
        "max_mb": 16000,
        "path": "contexts/hot",
        "storage": "memory_mapped"
      },
      "warm": {
        "description": "CPU memory, fast access",
        "max_mb": 64000,
        "path": "contexts/warm",
        "storage": "memory_mapped"
      },
      "cold": {
        "description": "SSD persistent, slower access",
        "max_mb": 500000,
        "path": "contexts/cold",
        "storage": "file",
        "compression": "lz4"
      }
    },

    "tiering": {
      "promote_threshold_accesses": 10,
      "demote_hot_to_warm_seconds": 300,
      "demote_warm_to_cold_seconds": 3600,
      "evict_cold_after_seconds": 86400
    },

    "defaults": {
      "ttl_seconds": 86400,
      "priority": "normal"
    },

    "eviction_policy": "lru",
    "max_contexts": 10000
  },

  "runtime": {
    "path": "runtime",
    "lock_timeout_ms": 5000,
    "state_sync_interval_seconds": 10
  }
}
```

### Custom Drive Configuration Example

```json
{
  "version": "1.0",

  "models": {
    "path": "D:\\AI\\Models",
    "max_loaded": 3
  },

  "contexts": {
    "path": "E:\\SnapLLM\\Contexts",

    "tiers": {
      "hot": {
        "max_mb": 20000,
        "path": "E:\\SnapLLM\\Contexts\\hot"
      },
      "warm": {
        "max_mb": 128000,
        "path": "E:\\SnapLLM\\Contexts\\warm"
      },
      "cold": {
        "max_mb": 2000000,
        "path": "F:\\SnapLLM\\ColdStorage",
        "compression": "zstd"
      }
    }
  }
}
```

---

## Memory Tiering Storage

### Tier Storage Formats

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    TIER STORAGE FORMATS                                 │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   HOT TIER (GPU-ready)                                                 │
│   ────────────────────                                                 │
│   Format: Memory-mapped, uncompressed                                  │
│   Extension: .kvc                                                      │
│   Access: Direct GPU DMA transfer possible                             │
│   Layout: [Header][KV Data aligned to 256 bytes]                       │
│                                                                         │
│   WARM TIER (CPU memory)                                               │
│   ──────────────────────                                               │
│   Format: Memory-mapped, optional LZ4 compression                      │
│   Extension: .kvc or .kvc.lz4                                          │
│   Access: Fast CPU access, GPU requires copy                           │
│   Layout: [Header][Compressed/Uncompressed KV Data]                    │
│                                                                         │
│   COLD TIER (SSD persistent)                                           │
│   ──────────────────────────                                           │
│   Format: File-based, ZSTD compressed                                  │
│   Extension: .kvc.zst                                                  │
│   Access: Load on demand, decompress to warm/hot                       │
│   Layout: [Header][ZSTD Compressed KV Data]                            │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### KV Cache File Format (.kvc)

```cpp
// KV Cache file format specification

struct KVCacheFileHeader {
    char magic[4] = {'S', 'K', 'V', 'C'};  // "SKVC" - SnapLLM KV Cache
    uint32_t version = 1;
    uint32_t flags;                         // Compression, quantization flags

    // Context metadata
    char context_id[64];
    char model_id[64];
    uint64_t created_timestamp;

    // KV shape
    uint32_t num_layers;
    uint32_t num_heads;
    uint32_t head_dim;
    uint32_t sequence_length;
    uint32_t dtype;                         // 0=fp32, 1=fp16, 2=bf16, 3=int8

    // Checksums
    uint64_t data_size;
    uint32_t header_checksum;
    uint32_t data_checksum;

    // Reserved for future use
    uint8_t reserved[64];
};

// File layout:
// [KVCacheFileHeader - 256 bytes]
// [Layer 0 Keys   - num_heads * seq_len * head_dim * dtype_size]
// [Layer 0 Values - num_heads * seq_len * head_dim * dtype_size]
// [Layer 1 Keys   - ...]
// [Layer 1 Values - ...]
// ...
// [Layer N Keys   - ...]
// [Layer N Values - ...]
```

---

## Workspace Management API

### REST Endpoints

```http
# Get workspace info
GET /api/v1/workspace/info

Response:
{
  "home": "C:\\Users\\user\\AppData\\Local\\SnapLLM",
  "models": {
    "path": "C:\\Users\\user\\AppData\\Local\\SnapLLM\\models",
    "count": 3,
    "total_size_mb": 21504
  },
  "contexts": {
    "path": "C:\\Users\\user\\AppData\\Local\\SnapLLM\\contexts",
    "tiers": {
      "hot": {"count": 5, "size_mb": 4500},
      "warm": {"count": 12, "size_mb": 8900},
      "cold": {"count": 156, "size_mb": 45000}
    }
  }
}

# List models in workspace
GET /api/v1/workspace/models

# List contexts in workspace
GET /api/v1/workspace/contexts?tier=hot

# Get workspace config
GET /api/v1/workspace/config

# Update workspace config
PATCH /api/v1/workspace/config
{
  "contexts": {
    "tiers": {
      "hot": {"max_mb": 20000}
    }
  }
}

# Trigger workspace maintenance
POST /api/v1/workspace/maintenance
{
  "operation": "compact",       // compact, verify, repair
  "target": "contexts"          // models, contexts, all
}

# Export workspace info
GET /api/v1/workspace/export?format=json
```

### CLI Commands

```bash
# Initialize workspace
snapllm workspace init [--path /custom/path]

# Show workspace info
snapllm workspace info

# List models
snapllm workspace models

# List contexts
snapllm workspace contexts [--tier hot|warm|cold]

# Compact cold storage
snapllm workspace compact --tier cold

# Verify integrity
snapllm workspace verify

# Move workspace
snapllm workspace move --to /new/path

# Set custom paths
snapllm config set models.path "D:\\Models"
snapllm config set contexts.tiers.cold.path "F:\\ColdStorage"
```

---

## Summary: Parallel Workspace Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    WORKSPACE ARCHITECTURE SUMMARY                       │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   SNAPLLM_HOME (e.g., C:\Users\user\AppData\Local\SnapLLM)             │
│   │                                                                    │
│   ├── models/              ← MODEL WORKSPACE (L1)                      │
│   │   ├── registry.json    ← Index of all models                       │
│   │   └── <model>/         ← Per-model directory                       │
│   │       ├── model.gguf   ← Weights                                   │
│   │       └── metadata.json                                            │
│   │                                                                    │
│   ├── contexts/            ← CONTEXT WORKSPACE (L2)                    │
│   │   ├── registry.json    ← Index of all contexts                     │
│   │   ├── hot/             ← GPU-ready tier                            │
│   │   ├── warm/            ← CPU memory tier                           │
│   │   ├── cold/            ← SSD persistent tier                       │
│   │   └── metadata/        ← Context metadata                          │
│   │                                                                    │
│   ├── runtime/             ← RUNTIME STATE                             │
│   └── config/              ← CONFIGURATION                             │
│                                                                         │
│   KEY PRINCIPLES:                                                      │
│   ─────────────────                                                    │
│   ✓ Parallel structure for models and contexts                         │
│   ✓ Each has its own registry, storage, metadata                       │
│   ✓ Context workspace adds tiering (hot/warm/cold)                     │
│   ✓ Platform-specific defaults, user-configurable                      │
│   ✓ Survives restarts (persistent storage)                             │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

*Document generated for SnapLLM Project - January 2026*
