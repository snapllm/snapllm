# GPU recovery audit — 2026-08-11

The lifecycle accounting tests cover saturating VRAM subtraction, budget
eviction predicates, model-size failure handling, and context-lifetime unload
blocking. They pass in the existing test suite when run from a configured
Release build.

The current implementation has these explicit boundaries:

- Rebalancing evicts least-recently-used resident models only when a configured
  VRAM budget exists; unknown capacity is fail-closed and does not evict.
- Recovery reloads an evicted model from its registered path and returns a
  boolean failure instead of claiming success.
- There is no hardware failure injection or multi-GPU failover test in this
  Windows CPU environment. A real GPU recovery claim requires a CUDA runner.
- Model switching and cold-load measurements are covered separately in the
  model-switching QA report.

## Reproduction evidence (2026-08-11)

The cancellation contract check passes:

```text
PASS gate RAII coverage: 8 acquisitions, 8 guards
PASS cached-context disconnect propagation
```

The existing `build_cpu` directory is not a clean configuration. A Release
CLI link attempt compiled the SnapLLM static library, then failed with MSVC
`LNK2038` (`_ITERATOR_DEBUG_LEVEL` and `RuntimeLibrary`) and unresolved debug
CRT symbols from stale Debug `mtmd`/llama objects. This prevents claiming a
green CLI build from that directory; use a fresh Release build directory for
the lifecycle executables.

No production code was changed by this audit. GPU failure injection,
multi-GPU failover, and failed-reload recovery remain untested on this CPU
host and require explicit seams plus a CUDA-capable runner.
