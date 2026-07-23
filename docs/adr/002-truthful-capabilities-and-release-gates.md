# ADR-002: Gate public capabilities on implementation evidence

**Date:** 2026-07-23 · **Status:** accepted
**Deciders:** Chief Architect · Head of AI advisory co-sign

## Context

The public tree advertises sub-millisecond switching, full API compatibility, multimodal/video generation, dequantization, prefetch, persistent context behavior, and enterprise security surfaces while several implementations are deferred, synthetic, simulated, or measured with incomplete benchmarks. CI mainly proves compilation and a health response.

## Decision

We will expose and document a capability only when its end-to-end path has a registered automated test or a reproducible hardware/model validation record. Placeholder paths return an explicit unsupported response and UI controls remain disabled with clear text. Switching is measured from request to first usable token with residency and memory assertions, not by timing a pointer/name assignment. CI and release workflows enforce product tests, types, lint, format, dependency audits, version consistency, and artifact smoke tests.

## Alternatives considered

| Option | Pros | Cons | Why not |
|---|---|---|---|
| Chosen: evidence-gated capabilities | Honest public contract; prevents placeholder regressions | Some features become temporarily unavailable | — |
| Keep demos behind undocumented flags | Retains visible breadth | Operators still mistake demonstrations for controls; untested code remains exposed | Rejected |
| Finish every advertised engine before securing release | Maximum feature breadth | Delays critical security remediation and couples unrelated engines | Security closure comes first |
| Documentation-only disclaimers | Fast | Does not remove unsafe or misleading runtime behavior | Insufficient |

## Consequences

- Positive: runtime, UI, documentation, and release claims converge on one tested truth.
- Positive: missing model/hardware coverage fails as unsupported rather than fabricated success.
- Negative: feature count may shrink until implementations and proof exist.
- Invalidation triggers: a capability gains an executable validation harness and passes fresh G4/G5 review.
