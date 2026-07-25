# QA Report: serving resilience 1.16.0

## Evidence

- CPU Release build (MSVC, CUDA disabled): passed.
- `ctest --test-dir build -C Release --output-on-failure`: 7/7 passed.
- `node scripts/check_versions.mjs`: `version_consistency: 1.16.0`.
- `snapllm_request_router_test`: covers text selection, vision selection,
  wrong-route rejection, missing model, and task routing.
- `snapllm_benchmark_throughput`: built as an opt-in end-to-end HTTP benchmark.

## Not claimed

No GPU hardware was available in this run, so GPU rebalance/failure recovery
and throughput numbers require a follow-up run on the target GPU host.
