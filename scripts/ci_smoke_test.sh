#!/usr/bin/env bash
set -euo pipefail

SNAPLLM_PATH="${1:?usage: ci_smoke_test.sh <snapllm_path> [port] [timeout_sec]}"
PORT="${2:-6930}"
TIMEOUT="${3:-30}"

if [[ ! -x "$SNAPLLM_PATH" ]]; then
  echo "snapllm binary not found or not executable: $SNAPLLM_PATH" >&2
  exit 1
fi

"$SNAPLLM_PATH" --server --port "$PORT" &
PID=$!

cleanup() {
  kill "$PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

for _ in $(seq 1 "$TIMEOUT"); do
  if curl -fsS "http://localhost:$PORT/health" >/dev/null 2>&1; then
    exit 0
  fi
  sleep 1
done

echo "Health endpoint did not become ready within ${TIMEOUT}s" >&2
exit 1