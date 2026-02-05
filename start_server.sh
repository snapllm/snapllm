#!/bin/bash
# =============================================================================
# SnapLLM Server Launcher (Linux/macOS)
# =============================================================================
# Starts the native SnapLLM HTTP server
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

HOST="${SNAPLLM_HOST:-127.0.0.1}"
PORT="${SNAPLLM_PORT:-6930}"
WORKSPACE_ROOT="${SNAPLLM_WORKSPACE_ROOT:-}" 

# Locate snapllm binary
EXE_PATH=""
if [ -n "${SNAPLLM_SERVER_EXE:-}" ] && [ -f "$SNAPLLM_SERVER_EXE" ]; then
  EXE_PATH="$SNAPLLM_SERVER_EXE"
  echo "[OK] Using SNAPLLM_SERVER_EXE"
fi

if [ -z "$EXE_PATH" ]; then
  for d in $(ls -dt build_codex_gpu* 2>/dev/null); do
    if [ -f "$d/bin/snapllm" ]; then
      EXE_PATH="$d/bin/snapllm"
      echo "[OK] Found Codex GPU build: $d"
      break
    fi
  done
fi

if [ -z "$EXE_PATH" ] && [ -f "build_gpu/bin/snapllm" ]; then
  EXE_PATH="build_gpu/bin/snapllm"
  echo "[OK] Found GPU build"
elif [ -z "$EXE_PATH" ] && [ -f "build_cpu/bin/snapllm" ]; then
  EXE_PATH="build_cpu/bin/snapllm"
  echo "[OK] Found CPU build"
elif [ -z "$EXE_PATH" ] && [ -f "bin/snapllm" ]; then
  EXE_PATH="bin/snapllm"
  echo "[OK] Found bin/snapllm"
fi

if [ -z "$EXE_PATH" ]; then
  echo "[ERROR] snapllm binary not found. Build first with ./build.sh"
  exit 1
fi

echo "[INFO] Executable: $EXE_PATH"
echo "[INFO] Server URL: http://$HOST:$PORT"

# Build arguments
ARGS=("--server" "--host" "$HOST" "--port" "$PORT")
if [ -n "$WORKSPACE_ROOT" ]; then
  ARGS+=("--workspace-root" "$WORKSPACE_ROOT")
fi

# Start server (blocking)
"$EXE_PATH" "${ARGS[@]}"
