# Docker local smoke test — 2026-08-15

Image: `snapllm:1.17.36-cpu`, built locally from the release tree with Docker
Desktop Linux engine 29.6.1. The image was run as a non-root user with
`D:\\Models` mounted read-only at `/models` and an API key supplied through the
environment.

Results:

- `/health`: HTTP 200, version `1.17.36`.
- `POST /api/v1/models/scan` for `/models`: HTTP 200, 48 models discovered.
- `POST /api/v1/models/load` for `LFM2.5-1.2B-Instruct-Q5_K_M.gguf`: success,
  14.1 seconds, CPU mode.
- Non-streaming `/v1/chat/completions`: HTTP 200, returned `Hello.`.
- Streaming `/v1/chat/completions`: HTTP 200, `text/event-stream`, 10 SSE
  lines including `data: [DONE]`.
- `/api/v1/models` after inference: HTTP 200.

The first image test exposed Windows-only backslash composition in the vPID
and diffusion workspace paths. Those paths now use `std::filesystem`, and the
retest above completed model load and both completion modes successfully.

The published `1.17.18` image additionally exposed a portability failure: its
CI-built binary exited with signal 132 (`AMX is not ready to be used`) on this
host during model load. Docker build configuration now sets `GGML_NATIVE=OFF`
for both CPU and CUDA images. A local `1.17.19-cpu` rebuild then loaded the
same model and completed chat successfully without the illegal-instruction
exit.
