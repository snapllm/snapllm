# Cancellation and timeout loop evidence — 2026-08-11

## Acceptance criterion

Cancellation, client disconnect, and timeout paths must release inference
gates and must not leave active inference slots stuck.

## Evidence collected

`src/server.cpp` has eight HTTP inference-gate acquisition sites. Each has an
immediate `InferenceGateGuard` whose destructor calls
`release_inference_gate()`. The timeout branch returns before incrementing the
active count. Direct OpenAI and Anthropic streaming callbacks return `false`
when `DataSink::is_writable()` is false, allowing the underlying generation to
stop and the RAII guard to run.

The deterministic contract harness is:

```text
python tests/cancellation_contract_test.py
```

The gate coverage check passes. The cached-context cancellation check currently
fails by design and exposes the remaining production defect:

```text
PASS gate RAII coverage: 8 acquisitions, 8 guards
FAIL cached-context disconnect is not propagated: bridge callback always returns true
```

## Blocker

`ContextManager::TokenCallback` is a `void` callback. In
`src/context_manager.cpp`, `query_streaming()` adapts it to the bridge's bool
callback but unconditionally returns `true`. The cached-context HTTP stream
therefore continues token generation after the client disconnects. The HTTP
gate remains held until generation naturally finishes, so this path does not
yet satisfy cancellation/timeout cleanup under load.

## Required production change (out of this QA-only slice)

Give `ContextManager::TokenCallback` a cancellation result (or add a separate
cancel predicate/token) and return that result from the bridge adapter. The
cached-context server callback must return `false` when the sink is no longer
writable. Add an integration test that disconnects an SSE client and verifies a
second request can acquire the gate before the model's full token budget is
exhausted.

`release_inference_gate()` also decrements unconditionally. The RAII guard is
non-copyable and currently prevents double release, but an explicit underflow
assert/guard would make the invariant observable in tests.
