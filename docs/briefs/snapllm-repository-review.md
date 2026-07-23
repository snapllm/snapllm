# Project Brief — Complete SnapLLM Repository Review

**Date:** 2026-07-23 · **Class:** L — cross-cutting architecture, implementation, security, tests, packaging, and operations · **Requested by:** human

## Problem

SnapLLM needs an evidence-backed assessment of whether its current repository and release candidate match its public claims and are safe, testable, maintainable, and releasable.

## Scope

- In: first-party C++, desktop/Tauri code, bindings, tests, CI/release, packaging, documentation, security boundaries, and vendored-code integration/provenance.
- **Non-goals:** implement fixes, alter public APIs, install dependencies, publish a release, or perform destructive/live exploitation.
- Deferred: remediation design and implementation after the human accepts priorities.

Vendored inference engines were reviewed at their SnapLLM integration and supply-chain boundaries, not line-by-line.

## Acceptance criteria

1. Repository structure and public surfaces are mapped with coverage limitations disclosed.
2. Available builds, tests, lint, type, dependency, and smoke checks are executed with real results.
3. Code, security, QA, and project reviewers independently return severity-ranked findings.
4. Release readiness receives explicit gate verdicts and a prioritized remediation direction.
5. Review artifacts are filed under `docs/` without modifying product behavior.

## Constraints

The starting worktree was already dirty and included user-owned modified, deleted, and untracked files. Those files must be preserved. No browser runtime, Graphify installation, Rust dependency cache, Gitleaks, Semgrep, Trivy, cargo-audit, or clang-tidy was available.

## Assumptions

- The checked-out `main` commit is the candidate under review.
- `enterprise=off` and `startup=off`; no repository profile declared otherwise.
- The AI/network surface still warrants advisory Head of AI and CISO governance.

## Open questions

- None blocked the review. Missing tools are recorded as coverage gaps, not silently treated as passes.

**Human confirmed:** request received 2026-07-23
