# Implementation Plan — SnapLLM Public Repository Remediation

**Brief:** [public repository remediation](../briefs/snapllm-public-repository-remediation.md)
**Design:** [ADR-001](../adr/001-secure-api-trust-boundary.md), [ADR-002](../adr/002-truthful-capabilities-and-release-gates.md) · **Class:** L

## Slices

| # | Slice | Evidence | Status |
|---|---|---|---|
| 1 | Security policy helpers and C++ tests | CTest boundary matrix | done |
| 2 | HTTP auth, Host/Origin controls, receipt timeout, shell-free launch | live adversarial HTTP test | done |
| 3 | Canonical path roots and bounded inference/multimodal input | unit and live boundary tests | done |
| 4 | Atomic, checked `.kvc` persistence | malformed/truncated/overflow/checksum tests | done |
| 5 | Context ownership, locking, lifecycle, and typed errors | lifecycle/persistence tests | done |
| 6 | Model ownership, unload accounting, truthful switching behavior | Release-active lifecycle test and claim sweep | done |
| 7 | Desktop auth/config/context request contracts | Vitest, lint, TypeScript | done |
| 8 | Desktop CSP, capability narrowing, and truthful surfaces | build, Tauri check/test, and Playwright browser journeys | done |
| 9 | Dependency and quality gates | npm/RustSec audits, CTest, Rust and desktop gates | done |
| 10 | Unified version, packaging, quickstart, and release workflow | version check and packaged archive smoke gates | done |
| 11 | Fresh G4/G5/QA verification | independent reviewer verdicts | verifying |

## Threat sketch

Assets are process privileges, model/workspace files, GPU/CPU/RAM, prompts, API
tokens, the desktop trust boundary, and release credentials. Entry points are
HTTP, Host/Origin headers, JSON bodies, paths, model/cache files, persisted
configuration, renderer capabilities, dependencies, and workflows.

## Risks and rollback points

- Authentication compatibility: retain Bearer and `X-API-Key`; never roll back
  the non-loopback security boundary.
- Path confinement: correct the import workflow if needed; never weaken root
  confinement to preserve an unsafe direct-load workflow.
- Concurrency: prefer correctness and owned lifetimes; optimize only from
  measured evidence.
- Dependency upgrades: keep reviewed lockfiles and revert only an isolated
  upgrade if its validation fails.
- Dirty baseline: stage only remediation files; preserve unrelated user files.

## Deviations and status

- Graphify was unavailable; explicit route/build metadata formed the
  architecture baseline.
- The in-app browser backend was unavailable, so the user-authorized local
  Playwright runner was used. Seven Chromium journeys cover desktop/mobile
  layout, all public routes, proxy security, the five UI states, offline
  transition, and real TinyLlama Chat inference.
- Publishing remains human-gated. No release, push, or external mutation is part
  of this plan.
