#!/usr/bin/env bash
set -euo pipefail

SNAPLLM_PATH="${1:?usage: model_smoke_test.sh <snapllm_path> [port] [timeout_sec]}"
PORT="${2:-6930}"
TIMEOUT="${3:-60}"

REQUIRED=(SNAPLLM_MODEL1_ID SNAPLLM_MODEL1_PATH SNAPLLM_MODEL2_ID SNAPLLM_MODEL2_PATH)
missing=()
for v in "${REQUIRED[@]}"; do
  if [[ -z "${!v:-}" ]]; then
    missing+=("$v")
  fi
done
if [[ ${#missing[@]} -gt 0 ]]; then
  echo "Skipping model smoke test; set env vars: ${REQUIRED[*]}" >&2
  exit 0
fi

if [[ ! -x "$SNAPLLM_PATH" ]]; then
  echo "snapllm binary not found or not executable: $SNAPLLM_PATH" >&2
  exit 1
fi

"$SNAPLLM_PATH" --server --port "$PORT" --load-model "$SNAPLLM_MODEL1_ID" "$SNAPLLM_MODEL1_PATH" &
PID=$!

cleanup() {
  kill "$PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

for _ in $(seq 1 "$TIMEOUT"); do
  if curl -fsS "http://localhost:$PORT/health" >/dev/null 2>&1; then
    break
  fi
  sleep 1
done

if ! curl -fsS "http://localhost:$PORT/health" >/dev/null 2>&1; then
  echo "Health endpoint did not become ready within ${TIMEOUT}s" >&2
  exit 1
fi

curl -fsS -X POST "http://localhost:$PORT/api/v1/models/load" \
  -H "Content-Type: application/json" \
  -d "{\"model_id\": \"$SNAPLLM_MODEL2_ID\", \"file_path\": \"$SNAPLLM_MODEL2_PATH\"}" >/dev/null

curl -fsS -X POST "http://localhost:$PORT/api/v1/models/switch" \
  -H "Content-Type: application/json" \
  -d "{\"model_id\": \"$SNAPLLM_MODEL2_ID\"}" >/dev/null