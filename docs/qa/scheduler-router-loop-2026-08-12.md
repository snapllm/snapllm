# Load-aware request router loop — 2026-08-12

## Scope

Add deterministic, thread-safe load-aware model selection without changing
the existing explicit-model routing contract.

## Implementation

`RequestRouter::choose` now has an overload accepting a point-in-time
`std::vector<ModelLoad>` snapshot (`in_flight`, `average_latency_ms`) and an
explicit round-robin cursor. Automatic requests choose compatible models by
lowest in-flight count, then lowest observed latency. Exact ties rotate by the
cursor. Explicit `requested_model` selections remain pinned and are validated
as before. No mutable global scheduler state is introduced; callers can read
their metrics under their own lock and pass one consistent snapshot.

## Verification

```text
cmake --build build_qa_router --config Release --target snapllm_request_router_test
ctest --test-dir build_qa_router -C Release -R request_router --output-on-failure
1/1 Test #7: request_router ...................   Passed    0.25 sec
100% tests passed, 0 tests failed out of 1
```

Coverage includes least-loaded selection, latency tie-breaking, deterministic
round-robin ties, explicit pinning, malformed load snapshots, and the existing
concurrent stateless routing matrix.

## Deliberate boundary

This slice does not wire metrics collection into the HTTP server or claim
continuous batching/multi-GPU balancing. Those require a separate scheduler
owner and admission contract.
