# ADR 004: Local serving resilience and routing

## Decision

SnapLLM keeps inference serialized at the GPU boundary, adds deterministic
request routing based on loaded-model capabilities, and exposes LRU GPU
rebalancing plus reload recovery through `ModelManager`. Routing rejects a
model/modality mismatch with HTTP 422 instead of silently sending work to the
wrong backend. Streaming cancellation remains cooperative through the
existing callback and `DataSink::is_writable()` check.

## Scope and limits

The rebalancer acts only when a configured VRAM budget is known; it never
evicts speculatively when capacity is unknown. The throughput benchmark is
opt-in and measures a running HTTP server. It does not turn CPU-only builds
into GPU benchmarks or claim multi-GPU balancing.
