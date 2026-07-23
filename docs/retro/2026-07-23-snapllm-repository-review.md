# Retrospective — Complete SnapLLM Repository Review · 2026-07-23

## Facts

Shipped: no product/release change; review artifacts only.
Gates walked: G0 scope, G1 evidence/architecture, G2 review plan, G3 inspection and execution, G4 independent code review, G5 independent QA/security, G6 readiness assessment, G7 record.
Result: code review returned to build, QA failed, security vetoed, G6 release readiness was refused, and the tagged release's historical G7 evidence was incomplete. This document records the present audit; it does not retroactively satisfy the `v1.3.0` release process.

## What worked

- Independent reviewers found mutually reinforcing defects in security, model lifecycle, context lifetime, CI, and public claims.
- Real commands separated “builds” from “tested” and exposed CI false-green conditions.
- Dirty-tree preservation and explicit coverage gaps prevented review activity from being mistaken for product changes or assurance.
- Direct data-flow inspection established the critical stored-command-injection chain without executing a harmful exploit.

## What failed or hurt

- Required Graphify coverage could not run because the module is absent.
- Product tests are not registered; desktop test/lint/type gates are absent or broken.
- C++ linking failed twice in one pass and succeeded in another, undermining reproducibility.
- Browser, Rust audit/build, formal secret scanning, static analysis, sanitizers, and fuzzing were unavailable.
- Documentation and UI presentation substantially overstate implemented security and enterprise behavior.
- The documented Linux `./build.sh --cuda` command silently chooses the CPU branch because the script recognizes only `gpu`.

## Root causes

- Security defects escaped because privileged routes were designed as trusted-local without enforcing that trust boundary, then paired with permissive browser access and shell execution.
- Correctness defects escaped because lifecycle, concurrency, serialization, and client/server contracts lack behavioral regression tests.
- Performance overclaim escaped because the benchmark times a cheap state assignment rather than user-visible readiness/TTFT and is not a release gate.
- Release drift escaped because versioning, dependencies, provenance, and product checks have no single enforced pipeline.
- Coverage gaps persist because review and release tooling is not bootstrapped reproducibly in the repository.

## Doctrine and tooling amendments

| # | Amendment | Target | Status |
|---|---|---|---|
| 1 | Add authenticated admin-boundary and cross-origin negative integration tests | server CI | proposed |
| 2 | Add ASan/TSan context lifecycle and malformed-cache fuzz targets | CMake/CI | proposed |
| 3 | Require desktop test, lint, `tsc --noEmit`, Rust fmt/check/test, and npm audits | CI/release | proposed |
| 4 | Measure switch-to-first-token latency with residency/memory assertions | benchmark/release gate | proposed |
| 5 | Generate versions from one source and validate artifacts at release | build/release | proposed |
| 6 | Bootstrap/pin Graphify, secret scanning, SAST, SBOM, provenance, and dependency audits | review/release tooling | proposed |

## Honest negative findings worth remembering

- A successful Vite bundle is not evidence that the TypeScript application type-checks or has tests.
- `ctest` exit 0 is not a pass when it reports no tests.
- A later successful C++ build does not erase two earlier linker failures; reproducibility remains unresolved.
- “Localhost” is not an authentication or browser security boundary.
- Doctrine tooling's 156 passing tests validate the added doctrine support tooling, not SnapLLM product behavior.
