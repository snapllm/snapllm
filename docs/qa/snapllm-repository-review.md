# QA + Security Report — Complete SnapLLM Repository Review @ 0ac6e430684d59fc5c8521e12fea399a9f0ff0df

**QA Engineer · Security Reviewer · Date:** 2026-07-23
**Build under test:** `0ac6e430684d59fc5c8521e12fea399a9f0ff0df` / `v1.3.0-dirty`

## Command evidence

| Check | Observed result | Pass |
|---|---|---|
| `npm run build` | exit 0; Vite 5.4.20, 3956 modules; JS 2,136.93 kB / 636.94 kB gzip; chunk warning | yes, build only |
| `npm test -- --run` | exit 1: no test files found | no |
| `npm run lint` | exit 1: `'eslint' is not recognized` | no |
| TypeScript `tsc --noEmit` | exit 1: numerous application/type contract errors | no |
| `ctest --test-dir build_cpu -C Release --output-on-failure` | exit 0: `No tests were found!!!` | no |
| C++ build, main pass | two linker failures (`llama-context.obj`, then `unicode-data.obj`) | no |
| C++ build, independent QA pass | exit 0; CLI help, Python binding import, and server health smoke exit 0 | inconsistent |
| Server health smoke | `{"status":"ok","version":"1.1.0","timestamp":"2026-07-23T17:03:42Z","models_loaded":0,"current_model":null}` | smoke only |
| `cargo fmt -- --check` | exit 1 on `desktop-app/src-tauri/src/main.rs` | no |
| `cargo check --locked --offline` / Rust tests | failed: cached `alloc-no-stdlib v2.0.4` unavailable | unverified |
| `npm audit --omit=dev --audit-level=low` | exit 1: 11 production vulnerabilities; 6 high, 5 moderate | no |
| Full `npm audit --json` | exit 1: 23 vulnerabilities; 2 critical, 14 high, 6 moderate, 1 low | no |
| Doctrine tooling unit suite | 156 tests, OK | yes; not product coverage |
| Doctrine lint/verify | clean/green, consumer tests skipped as applicable | yes; not product coverage |

The main and independent C++ results disagree while using the same build tree. This is recorded as build non-determinism/flakiness, not converted into a pass.

## Adversarial and edge matrix

| Input/scenario | Expected | Observed | Pass |
|---|---|---|---|
| Cross-origin privileged POST | authenticated and rejected by default | arbitrary origin reflected; no auth gate | no |
| Host with shell metacharacters | strict parse/reject; never sent to shell | persisted then interpolated into `std::system` on restart | no |
| Negative/huge token or image settings | bounded validation | unchecked or unsigned conversion paths | no |
| Huge batch/body/image | explicit size/cardinality caps | no comprehensive caps/timeouts found | no |
| Concurrent context query/delete | owned lifetime and synchronized access | non-owning view can outlive lock/storage | no |
| Malformed `.kvc` | reject before allocation/use | dimensions/lengths trusted; checksum TODO | no |
| Unknown model context ingestion | 4xx | synthetic cache and HTTP 201 | no |
| Context operation failure | non-2xx typed error | error string returned with HTTP 200 | no |
| Desktop promote/demote | valid request contract | client sends empty body server parses as JSON | no |
| Documented `./build.sh --cuda` | configure CUDA or reject invalid mode | script recognizes only `gpu` and otherwise selects CPU | no |

No live RCE, destructive filesystem, or resource-exhaustion exploit was executed. The critical chain is established by direct data-flow inspection.

## Security sweep

- Secret scan: no high-confidence current/history first-party matches from heuristic scans; Gitleaks was unavailable, so this is not a formal secret-scan pass.
- Injection: critical stored OS command-injection chain found.
- Dependencies: npm production and full audits failed; Rust audit unavailable.
- Dangerous constructs: `std::system`, unauthenticated privileged routes, reflected CORS, attacker-controlled filesystem paths, unsafe deserialization, prompt logging.
- Bounds/fail-closed: failed for bodies, inference controls, images/batches, workspace files, and administrative exposure.
- Tools unavailable: Gitleaks, Trivy, Semgrep, cargo-audit, clang-tidy, Graphify, and browser runtime.

## Coverage gaps

- No real browser, accessibility, five-state UI, or desktop E2E run.
- Rust compile/tests and vulnerability audit are unverified due the offline cache/tool gap.
- No sanitizer, fuzz, leak, GPU, multi-platform packaging, upgrade, rollback, or performance-claim validation.
- Product test coverage is effectively absent from registered CI/CTest.

**Security verdict:** **VETO** — critical RCE and unauthenticated control plane, plus high-severity resource/path/deserialization issues.
**QA verdict:** **FAIL** — return to build.

**Project verdict:** **G6 REFUSED / NO-GO**; tagged-release G7 evidence is incomplete.
