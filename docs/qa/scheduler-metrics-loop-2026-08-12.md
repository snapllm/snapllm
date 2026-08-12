# Scheduler observability loop — 2026-08-12

## Scope

Expose bounded admission state and per-model in-flight counters through the
existing configuration and server-metrics endpoints, then surface the values
in the Metrics page without changing routing or admission behavior.

## Implementation

- `waiting_inference_count` tracks requests waiting on the inference gate and
  is decremented on both acquisition and timeout paths.
- `/api/v1/server/metrics` now returns `scheduler.active_inferences`,
  `scheduler.max_active_inferences`, `scheduler.waiting_inferences`, and the
  `admission` strategy, plus `models[].in_flight`.
- `/api/v1/config` exposes the same live scheduler gauges and the bounded
  queue limit.
- Metrics UI displays active slots, waiting requests, and admission mode.
- Per-model in-flight counters are RAII-balanced around model-backed request
  handlers, including exception and streaming completion paths.

## Verification

- CMake Debug build (`cmake --build build --config Debug -j 4`): passed.
- No new dependencies added.
- Queue remains bounded by the existing HTTP thread-pool limit; this change
  only adds observability.

## Notes

The queue depth is intentionally reported as requests waiting for an
inference slot, not an estimate of the internal httplib queue (which is owned
by the library). This avoids claiming visibility that the runtime does not
provide.
