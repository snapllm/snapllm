# Risk Register — SnapLLM

**Last review:** 2026-07-23 · **Next review:** before publishing v1.3.1 · **Owner:** Risk Officer
**Acceptance threshold:** any high-impact residual risk requires written human acceptance with an expiry.

## Open risks

| ID | Domain | Risk | Likelihood | Impact | Mitigation | Owner | Review by |
|---|---|---|---|---|---|---|---|
| R-005 | supply chain | Tauri 1 transitively retains 15 RustSec maintenance/unsoundness warnings despite no actionable vulnerability | medium | medium | Keep the documented allowlist narrow; plan a governed Tauri major migration | DevOps Engineer | next minor |
| R-007 | operational | Unrelated user-owned dirty files could be mixed into the remediation commit | medium | medium | Exact staging; exclude `video/`, `nul`, and `run1.json` | Engineering Lead | commit |

## Accepted risks

None. No risk has been human-waived.

## Closed this period

| ID | Closure evidence |
|---|---|
| R-001 | Loopback default, non-loopback key requirement, Host validation, exact Origin allowlist, preserved proxy Origin, evil-Origin HTTP regression |
| R-002 | Canonical roots, UNC/device rejection, bounded typed cache/workspace JSON, checksums, strict base64, bounded asset responses |
| R-003 | Owned context lifecycle, synchronized removal, unload accounting, shared lifecycle ownership, RAII inference slots, CTest lifecycle regressions |
| R-004 | Unsupported surfaces disabled or explicit; unproven performance claims removed or qualified |
| R-006 | CI now gates CTest, HTTP integration, lint, types, units, production build, Playwright, Rust checks/audit, and version consistency |

## Sources swept this review

2026-07-23 code review · security review · QA report · remediation plan · public release workflows · native/frontend/browser test evidence.
