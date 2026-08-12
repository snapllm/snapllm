# Scheduler throughput loop — 2026-08-12

## Controlled benchmark

The throughput benchmark was run against an intentionally unused port to verify
bounded client failure behavior:

```text
throughput.requests 8
throughput.concurrency 4
throughput.completed 0
throughput.failed 8
throughput.rps 0
latency.p50_ms 2002.15
latency.p95_ms 2005.79
LASTEXIT=1
```

This is a connection-refusal control, not a throughput claim. A real
multi-request throughput result requires a live daemon with a loaded model;
the benchmark correctly returns non-zero when every request fails.

## Live low-weight model benchmark

Using `D:\Models\LFM2.5-1.2B-Instruct-Q5_K_M.gguf` on CPU at port 6941, health
returned HTTP 200 before benchmarking. All requests completed successfully:

| Requests | Client concurrency | Completed | Failed | RPS | p50 | p95 |
|---:|---:|---:|---:|---:|---:|---:|
| 16 | 4 | 16 | 0 | 0.389953 | 9905.58 ms | 10767.6 ms |
| 16 | 8 | 16 | 0 | 0.347698 | 22648 ms | 23290.8 ms |
| 16 | 16 | 16 | 0 | 0.376135 | 21223.1 ms | 39943.2 ms |

The CPU run confirms bounded concurrent admission and no request loss, but
does not support GPU throughput claims. Higher client concurrency increases
tail latency because the configured inference gate is conservative and the
model is CPU-bound.

The benchmark now performs a `/health` preflight and prints the server's
scheduler configuration before sending requests. Use `--mode cpu` or
`--mode gpu` as an explicit label; the label does not change server settings.

The modified benchmark was rebuilt successfully in an isolated clean build
directory (`build_bench_clean`, Debug, CPU-only). Its preflight behavior was
verified against an unused port:

```text
benchmark.preflight_failed health=connection_failed
```

The same binary reached a live daemon and reported the expected control-plane
data before the request phase. The daemon was started with the documented
`--load-model low PATH` syntax (the model name is required):

```text
benchmark.mode cpu
benchmark.server_health 200
benchmark.scheduler_config ..."max_active_inferences":1..."queue_limit":64...
throughput.requests 1
throughput.concurrency 1
throughput.completed 1
throughput.failed 0
throughput.rps 0.409546
latency.p50_ms 2440.91
latency.p95_ms 2440.91
```

The loaded-model smoke run completed successfully. The benchmark now fails
early with a clear preflight result when the daemon is unavailable, and avoids
misleading throughput numbers in that case.

A concurrent loaded-model smoke run also completed without loss:

```text
throughput.requests 4
throughput.concurrency 2
throughput.completed 4
throughput.failed 0
throughput.rps 0.413999
latency.p50_ms 4780.75
latency.p95_ms 4813.46
```
