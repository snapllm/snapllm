# Project Brief — SnapLLM Public Repository Remediation

**Date:** 2026-07-23 · **Class:** L — cross-cutting security, correctness, UI, testing, CI, packaging, and public API remediation · **Requested by:** human

## Problem

SnapLLM is already public while the `v1.3.0` tree contains remotely and browser-reachable security vulnerabilities, unsafe lifecycle and persistence code, broken desktop/server contracts, absent product regression gates, inconsistent release identity, and public claims that exceed implemented behavior. Users may run or expose the software based on controls and guarantees that are not actually enforced.

## Scope

- In: HTTP trust boundary, authentication, origin/Host checks, shell and path safety, request bounds, context/cache persistence, concurrency and model lifecycle, desktop API wiring and truthful states, dependency remediation, product tests, CI/release hardening, version identity, documentation, and changelog.
- **Non-goals:** publish or push changes, rewrite shared Git history, add paid services, preserve unsafe unauthenticated remote behavior, or claim unfinished placeholder features as complete.
- Deferred only when externally blocked: GPU/model-specific validation requiring model artifacts or hardware unavailable on this host. Such surfaces must remain disabled or honestly documented until proven.

## Acceptance criteria

1. Cross-origin webpages cannot read or mutate the local API, and untrusted request data never enters a shell command.
2. Non-loopback servers refuse startup without a strong API key; protected routes enforce Bearer or `X-API-Key` authentication at the action boundary.
3. Request bodies, tokens, prompts, batches, images, dimensions, steps, paths, and serialized cache sizes are validated and bounded with adversarial regression tests.
4. Model/context ownership has no detached-use-after-destruction, non-owning cache view race, shared-lock mutation, duplicate server load, or unload leak.
5. Context ingestion/query/promotion/demotion contracts are truthful, return correct HTTP statuses, and match the desktop client.
6. Desktop types, lint, unit tests, build, and CSP/capability checks pass; simulated security controls are removed, disabled, or clearly labeled.
7. C++ product tests are registered and run by CTest; CI enforces C++ tests, desktop tests/lint/types, Rust checks, dependency audits, and version consistency.
8. One version source drives CLI, server, bindings, desktop, packages, and release artifacts.
9. Public documentation and benchmarks describe only measured, implemented behavior; unsupported video/dequantization/prefetch/RAG claims are disabled or labeled.
10. Final fresh code, QA, security, and project reviews have zero open blockers or unwaived majors before G6 entry.

## Constraints

- Preserve the pre-existing dirty worktree and unrelated user files.
- No new dependency without the dependency-policy gate.
- Every behavior fix requires a regression test.
- Public API security changes are within the human-requested remediation scope; unrelated API or license changes still require escalation.
- No external push, tag, release, or publication without explicit human approval.

## Assumptions

- Secure compatibility means same-origin packaged UI plus standards-compatible Bearer API keys, not preservation of insecure remote defaults.
- Local CLI clients may operate without a configured key only on a validated loopback bind and only while browser Origin/Host protections remain enforced.
- Placeholder capabilities will be disabled/documented before release if implementation and proof cannot be completed safely.

## Open questions

- None block local remediation. Publishing and any public security advisory remain separate human-approved actions.

**Human confirmed:** yes · 2026-07-23
