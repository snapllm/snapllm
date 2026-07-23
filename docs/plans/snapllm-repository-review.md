# Implementation Plan — Complete SnapLLM Repository Review

**Brief:** [brief](../briefs/snapllm-repository-review.md) · **Class:** L

## Slices

| # | Slice | Role | Files touched | Proven by | Status |
|---|---|---|---|---|---|
| 1 | Hydrate doctrine, classify, and establish scope | Engineering Lead | Review artifacts only | Doctrine routing and state status | done |
| 2 | Map architecture and public claims | Chief Architect | Review artifacts only | source/build/docs inspection | done; Graphify unavailable |
| 3 | Exercise build, tests, types, lint, smoke, and audits | QA Engineer | None | command evidence in QA report | done with failures/gaps |
| 4 | Independent code and security review | Code/Security Reviewers | None | severity-ranked findings | done; both rejected |
| 5 | Reconcile release readiness and file evidence | Project Reviewer | `docs/` review artifacts | final gate record | done |

## Threat sketch

Assets touched: host process privileges, local filesystem/model paths, GPU/CPU/RAM, prompts and conversations, model/workspace files, build and release credentials.

Entry points: unauthenticated HTTP routes, cross-origin browser requests, configuration persistence, model/workspace paths, inference parameters, serialized `.kvc` files, desktop-rendered content, dependencies, and release workflows.

Worst plausible abuse: a malicious webpage persists shell metacharacters through the configuration API and gains code execution when SnapLLM next opens its UI; exposed instances also permit unauthenticated administrative control and resource exhaustion.

## Risks and rollback points

- Review commands can generate build metadata → inspect status and remove only review-generated scratch output.
- Existing dirty tree can be overwritten → make no source edits and preserve the baseline status.
- Missing tooling can create false confidence → mark the affected coverage unverified.
- Parallel findings can conflict → retain the stricter verified conclusion and identify inconsistent build observations.

## Deviation log

- 2026-07-23: Graphify was unavailable (`No module named graphify`); used source, CMake, package, workflow, and route metadata explicitly.
- 2026-07-23: no browser runtime was available after browser setup; visual/accessibility/E2E checks remain unverified.
- 2026-07-23: C++ linking failed twice in the main pass but succeeded later in an independent verifier using the same build tree; recorded as non-deterministic build evidence, not a clean pass.

## Status snapshot

Done: complete review and independent gates. Next: remediate blockers and add regression tests. Blocked on: release is vetoed by critical security and correctness findings.
