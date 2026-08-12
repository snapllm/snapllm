# ADR 002: Bounded concurrent request scheduling

## Decision

Add load-aware, bounded scheduling around the existing HTTP worker pool and
inference gate. Routing remains deterministic for explicit model requests, but
automatic requests may choose the least-loaded compatible resident model using
in-flight and recent-latency metrics. Admission remains fail-closed: bounded
queues, request deadlines, cancellation propagation, and HTTP 503 on overload.

## Capability slices

- **Admission:** bounded queue, configurable HTTP workers, inference slots,
  timeout, cancellation, and queue metrics.
- **Routing:** explicit-model pinning; least-loaded compatible selection for
  automatic requests; deterministic tie-breaks.
- **Endpoint coverage:** OpenAI chat, Anthropic Messages, text generation, and
  batch generation use the same scheduler snapshot.
- **Fairness:** per-model in-flight accounting and round-robin tie-breaking;
  no starvation under equal load.
- **Batching:** preserve the existing batch endpoint. Token-level continuous
  batching is not claimed until the backend exposes a safe shared scheduler.
- **GPU:** retain explicit user-controlled slots; automatic GPU rebalancing and
  multi-GPU failover remain hardware-dependent and require a separate seam.

## Alternatives rejected

1. Unbounded request queues: rejected because overload becomes memory growth and
   hides caller backpressure.
2. Implicit model switching for explicit requests: rejected because it violates
   request semantics and can produce the wrong model output.
3. Parallel generation by default: rejected because GPU memory/thread safety is
   model- and hardware-dependent; default remains one active inference.

## Reopen triggers

Revisit when backend-level continuous batching or multi-GPU scheduling APIs are
available, or when production benchmarks show queue wait dominates generation.
