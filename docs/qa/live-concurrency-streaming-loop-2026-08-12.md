# Live concurrency and streaming QA loop — 2026-08-12

## Scope

Exercise the real HTTP daemon with the low-weight model
`D:\\Models\\LFM2.5-1.2B-Instruct-Q5_K_M.gguf`, including health/config
preflight, concurrent inference, SSE streaming, client cancellation, and
scheduler cleanup.

## Environment

- Executable: `build\\bin\\snapllm.exe` (v1.17.8)
- Launch: `--server --host 127.0.0.1 --port 6930 --gpu-layers 0`
- Model: `low-weight-ui-e2e`
- Benchmark: `build_bench_clean\\bin\\snapllm_benchmark_throughput.exe`

## Evidence

Health and config returned HTTP 200. The scheduler advertised
`max_active_inferences=1`, `waiting_inferences=0`, and `queue_limit=64`.

```text
benchmark.mode cpu
benchmark.server_health 200
throughput.requests 4
throughput.concurrency 2
throughput.completed 4
throughput.failed 0
throughput.rps 0.409295
latency.p50_ms 4855.32
latency.p95_ms 4916.76
```

```text
throughput.requests 8
throughput.concurrency 4
throughput.completed 8
throughput.failed 0
throughput.rps 0.373474
latency.p50_ms 9765.83
latency.p95_ms 11304.2

throughput.requests 8
throughput.concurrency 8
throughput.completed 8
throughput.failed 0
throughput.rps 0.378678
latency.p50_ms 10451.3
latency.p95_ms 18426.5
```

SSE streaming returned HTTP 200 with `Content-Type: text/event-stream`, two
content chunks, a stop chunk, and `[DONE]`; elapsed time was 2067.1 ms.

Client cancellation was simulated by closing the response after the first
SSE chunk. The subsequent metrics samples repeatedly reported
`active_inferences=0`, `waiting_inferences=0`, and the model `in_flight=0`,
showing no leaked scheduler permit.

## Findings

- Pass: 2-, 4-, and 8-way concurrent requests all completed without failures.
- Pass: SSE framing and terminal `[DONE]` marker are present.
- Pass: cancellation releases the active-inference slot.
- Fixed: `/api/v1/models` now derives the LLM device from tracked VRAM
  residency, reporting `cpu` for zero residency and `gpu` when layers are
  resident in VRAM. The native Debug target rebuilt successfully and the full
  CTest suite remained green (8/8).
- Focused Playwright verification against the rebuilt CLI passed:
  `1 passed (37.1s)`. The test loaded the low-weight model with
  `n_gpu_layers: 0`, observed `device: "cpu"`, and completed a Chat UI
  response. The daemon was started with explicit `127.0.0.1:9780` and
  `localhost:9780` CORS origins; it was stopped after the run.
- Regression coverage: `desktop-app/e2e/app.spec.ts` now asserts that the
  low-weight CPU load is listed with `device:"cpu"`; rerun the live Playwright
  test after rebuilding the daemon binary.
