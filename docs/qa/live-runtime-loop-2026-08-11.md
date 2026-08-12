# Live runtime loop — 2026-08-11

## Environment

- Binary: `build_core/bin/snapllm.exe`
- Model: `D:\Models\LFM2.5-1.2B-Instruct-Q5_K_M.gguf`
- Mode: CPU-only (`--gpu-layers 0`)
- API: `127.0.0.1:6930`

## Evidence

- `/health` returned `{"status":"ok","version":"1.17.8"}`.
- `/v1/models` listed the loaded model `lfm12`.
- A non-streaming chat request returned HTTP 200.
- Eight concurrent chat requests returned HTTP 200: `8/8` succeeded, `0` errors.
- A streaming request was disconnected after the first 256-byte chunk; a
  follow-up request returned HTTP 200 in `1.983s`, demonstrating that the
  inference gate was released.
- Server logs showed each request ending with
  `[SnapLLM Gate] Released inference slot (0/1 active)`.

The local test daemon was stopped after validation. No external service was
used and no release artifact was published.
