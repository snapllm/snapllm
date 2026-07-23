# Code Review — Complete SnapLLM Repository Review @ 0ac6e430684d59fc5c8521e12fea399a9f0ff0df

**Reviewer roles:** Code Reviewer, Security Reviewer, Project Reviewer · **Date:** 2026-07-23
**Reviewed against:** [brief](../briefs/snapllm-repository-review.md) and [plan](../plans/snapllm-repository-review.md)

## Executive verdict

**RETURN TO BUILD. DO NOT RELEASE OR NETWORK-EXPOSE THIS REVISION.**

The security reviewer issued a veto. The repository's central performance claim is not demonstrated by the implementation/benchmark, the HTTP control plane is unauthenticated, and a browser-reachable stored OS command-injection chain can execute on restart.

## Findings

| # | Sev | Location | Finding | Required direction | Resolution |
|---|---|---|---|---|---|
| 1 | [BLOCKER] | `src/server.cpp:468`, `src/server.cpp:518`, `src/server.cpp:1999`; `src/main.cpp:403` | Any origin can mutate an unauthenticated persisted host value that later enters `std::system` during UI auto-open: stored OS command injection/RCE. | Remove shell execution; strictly parse hosts; authenticate/authorize admin routes; enforce CSRF/origin/Host controls. | open |
| 2 | [BLOCKER] | `src/server.cpp:582`; `DEVELOPER_GUIDE.md:124`; `Dockerfile:36` | Privileged config, model, scan, cache, context, and inference routes have no server-side authentication. Reflected CORS makes localhost browser control practical; Docker binds publicly and runs as root by default. | Add real authn/authz, isolate admin routes, default loopback/non-root, and fail closed on unsafe binds. | open |
| 3 | [BLOCKER] | `src/vpid_bridge.cpp:715`; `src/model_manager.cpp:101`; `tests/benchmark_switching.cpp:94`; `README.md:49` | The advertised sub-millisecond switch measures an assignment after unconditional LRU eviction; actual reload is deferred until generation. The benchmark omits resulting TTFT and is not a CMake test target. | Define an end-to-end switching metric, fix residency behavior, and enforce it with reproducible tests before making the claim. | open |
| 4 | [BLOCKER] | `src/context_manager.cpp:235`, `src/context_manager.cpp:281`, `src/context_manager.cpp:398`, `src/context_manager.cpp:1211` | A detached thread captures `this`; non-owning KV views outlive the shared lock while concurrent deletion can erase storage; mutation also occurs under a shared lock. These create lifetime and data-race hazards. | Use owned lifetimes, join/stop workers, and establish a single correct synchronization model with sanitizer stress tests. | open |
| 5 | [BLOCKER] | `src/server.cpp:3383`; `src/context_manager.cpp:933`; `include/snapllm/kv_cache.h:98` | Context ingestion accepts unknown model IDs, fabricates a large zero-filled cache, and returns 201 “precomputed.” Input can drive extreme allocation. | Validate model/tokenizer identity, implement genuine precomputation, cap inputs, and fail honestly. | open |
| 6 | [BLOCKER] | `.github/workflows/ci.yml`; `desktop-app/package.json`; `src/CMakeLists.txt` | CI can be green while product tests are absent, TypeScript is red, lint is unavailable, and CTest registers zero tests. | Add required test/type/lint/sanitizer gates and a tracked regression suite before release. | open |
| 7 | [MAJOR] | `src/server.cpp:1119`, `src/server.cpp:2771`, `src/server.cpp:2837`, `src/server.cpp:3012`, `src/server.cpp:3145` | HTTP payloads, token counts, batch size, diffusion dimensions/steps, and vision inputs are insufficiently bounded; negative values can become huge unsigned work limits. | Apply checked parsing, multiplication, explicit caps, deadlines, concurrency lifetime controls, and rate limits. | open |
| 8 | [MAJOR] | `src/main.cpp:685`, `src/main.cpp:742` | Server mode creates/loads through one manager and then constructs/reloads through another while the first remains alive, risking duplicate VRAM use/OOM. | Use one owning model lifecycle. | open |
| 9 | [MAJOR] | `src/model_manager.cpp:93`; `src/vpid_bridge.cpp:1215` | Unload clears a dequantization cache but does not release the underlying llama model/VRAM; release occurs only at destruction. | Make unload release all owned inference resources and test memory recovery. | open |
| 10 | [MAJOR] | `src/context_manager.cpp:994`, `src/context_manager.cpp:1050`; `include/snapllm/kv_cache.h:342` | `.kvc` save is non-atomic and omits checksums; load trusts dimensions, lengths, and NUL termination before safe validation/allocation. | Atomic write/rename, exact reads, checked arithmetic/caps, version/type/file-size/checksum validation, fuzzing. | open |
| 11 | [MAJOR] | `src/context_manager.cpp:391`; `src/server.cpp:3598` | Context query functions encode failures as strings and the server returns HTTP 200 success. | Use typed results and accurate HTTP status/error bodies. | open |
| 12 | [MAJOR] | `desktop-app/src/lib/api.ts:1193`; `src/server.cpp:3660`; `desktop-app/src/pages/Settings.tsx:247`; `src/server.cpp:2003` | Promote/demote sends an empty JSON body the server parses; workspace settings use a client/server schema mismatch and are reported saved despite being ignored. | Share validated request schemas and add integration/contract tests. | open |
| 13 | [MAJOR] | `src/server.cpp:1171`; `src/vpid_bridge.cpp:1245` | Full conversation/system-prompt content and query fragments are logged. | Log metadata only by default; redact and govern any opt-in content logging. | open |
| 14 | [MAJOR] | `desktop-app/package.json`; `desktop-app/src-tauri/tauri.conf.json:59` | Production npm audit reports 11 vulnerabilities (6 high, 5 moderate); full audit reports 23 including 2 critical. Tauri CSP is null with broad capabilities. | Policy-governed upgrades plus CI audits; restrictive CSP and least-privilege scopes. | open |
| 15 | [MAJOR] | `desktop-app/src/pages/Security.tsx:60`; `desktop-app/src/pages/ApiKeys.tsx:51`; `desktop-app/src/store/index.ts:149` | UI presents MFA, auth, rate limiting, encryption, audit, keys, login, and agents as operational while much is hardcoded/local/simulated. | Remove or unmistakably label demos; show only server-enforced controls and test them. | open |
| 16 | [MAJOR] | `src/server.cpp:2134`, `src/server.cpp:2451` | Attacker-controlled model/scan paths lack root allowlists; Windows UNC paths can initiate SMB credential disclosure/relay. | Authenticate and canonicalize; reject UNC/device paths; enforce configured roots. | open |
| 17 | [MAJOR] | `CMakeLists.txt`; `src/server.cpp:195`; `src/main.cpp:199`; `bindings/snapllm_bindings.cpp:371`; `desktop-app/src-tauri/Cargo.toml` | Release identity drifts across 1.3.0, 1.1.0, 1.0.0, and 0.1.0. | Generate one version source and gate consistency. | open |
| 18 | [MAJOR] | `.github/workflows/release.yml`; `Dockerfile`; vendored trees | Release jobs have broad write scope; actions/images are mutable/unpinned; no secret/SAST/dependency/SBOM/provenance gates; vendored/model integrity lacks a provenance/hash policy. | Pin immutable inputs, minimize permissions, add security/provenance gates, SBOMs, attestations, and vendor/model ledgers. | open |
| 19 | [MAJOR] | `src/dequant_cache.cpp`; `src/prefetch_engine.cpp`; `src/context_manager.cpp`; `DOCUMENTATION.md` | Public/enterprise claims exceed implemented behavior; core files retain dummy/TODO paths and desktop enterprise surfaces are mocks or unrouted. | Replace claims with measured current behavior or complete/test the features. | open |
| 20 | [BLOCKER] | `RELEASE.md:15`; `.github/workflows/release.yml:340`; tracked `docs/` | The tagged `v1.3.0` candidate has no tracked brief, changelog, ADR, review, QA report, release checklist, or retrospective. The present worktree is dirty, so shipped provenance and the release Definition of Done cannot be reconstructed. | Establish a clean candidate and a tracked, commit-bound G0–G7/release evidence chain before release. | open |
| 21 | [MAJOR] | `README.md:154`; `build.sh:5`; `build.sh:64` | The documented Linux GPU command `./build.sh --cuda` is not a supported mode; the script recognizes only `gpu`, so the published command silently takes the CPU branch. | Reject unknown modes and align tested quick-start commands with the CLI contract. | open |

## Additional observations

- `src/server.cpp:566` returns raw `e.what()` to clients, exposing internal detail.
- OpenAI-compatible messages are flattened and rewrapped, losing structured role isolation (`src/server.cpp:1179`, `src/vpid_bridge.cpp:1276`).
- No first-party public-surface changelog exists, and quick-start/version documentation is inconsistent.
- The desktop production bundle is approximately 2.14 MB before compression and triggers Vite's chunk warning.

## Scope check

All first-party product, build, release, and documentation surfaces were included. Vendored engines were reviewed only where SnapLLM integrates, builds, updates, or asserts provenance. Graphify was unavailable, so graph-backed orphan/impact analysis is not claimed. Browser/accessibility behavior and Rust dependency health remain unverified due environment/tooling limitations.

## Verdict

- [ ] Pass
- [x] Return to build — open blockers and majors above

Security veto: active. Human override: none.

Project gate: **G6 REFUSED / NO-GO**. The tagged release's historical G7 record is **FAIL / INCOMPLETE**. This audit files a present-day review record but does not retroactively make `v1.3.0` compliant.
