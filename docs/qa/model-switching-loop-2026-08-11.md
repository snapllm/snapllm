# Model switching loop — 2026-08-11

## Setup

- CPU-disabled build with CUDA configuration enabled by default
- `LFM2.5-1.2B-Instruct-Q5_K_M.gguf`
- `Janus-Pro-1B.Q4_K_M.gguf`
- `snapllm_benchmark_switching --iterations 1`

## Evidence

Resident and warm-cache switching completed successfully:

- Resident switch P99: `0.0051 ms` — benchmark target passed.
- Warm-cache switch P99: `0.0049 ms` — benchmark target passed.
- Rapid two-model switch P99: `0.0049 ms` — benchmark target passed.

The first implementation mixed generation into the switch benchmark and reused
one manager across rapid-switch and cold-load phases. That produced Windows
failures on this host. The benchmark now isolates cold-load in a fresh manager,
uses CPU-only loading for deterministic lifecycle testing, and leaves inference
load to the throughput benchmark.

The isolated rerun completed successfully:

- Cold load: `26,817.8091 ms` (one iteration, truthful measurement).
- Four benchmark results passed, process exit code `0`.

The log still reports missing layer-tracking manifests; this is a cache metadata
warning and does not prevent loading. The loader now reports this expected
no-manifest state informationally when vDPE manifest generation is disabled,
and reserves an error for a manifest that disappears after being detected.

The switching benchmark is now a CMake target:
`snapllm_benchmark_switching`.
