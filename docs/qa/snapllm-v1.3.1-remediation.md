# QA Report — SnapLLM v1.3.1 Remediation

**Date:** 2026-07-23
**Class:** L
**Candidate:** working tree for `v1.3.1`

## Acceptance evidence

| Gate | Exact observed result |
|---|---|
| Native Release build | `cmake --build build-security --config Release --parallel 4` exited 0 and produced `build-security/bin/snapllm.exe` |
| Native regressions | CTest: `100% tests passed, 0 tests failed out of 6` |
| HTTP security integration | `server_security_integration: all checks passed` |
| Desktop lint and types | ESLint and `tsc --noEmit` exited 0 |
| Desktop units | Vitest: `1 passed`, `11 passed` |
| Desktop production build | Vite: `✓ built in 1.68s` |
| JavaScript dependency audit | `found 0 vulnerabilities` |
| Version consistency | `version_consistency: 1.3.1` |
| Browser journeys | Playwright Chromium: 3 passed (Chat/proxy/routes), 3 passed (model states/offline/desktop responsive), and 1 passed (Pixel 7 responsive) |

## Browser state and journey matrix

| State/journey | Evidence |
|---|---|
| Empty | Models route renders `No models loaded` from an empty server envelope |
| Loading | Delayed models response renders `Loading models...` |
| Error | HTTP 503 renders the explicit `Unable to load models` recovery state |
| Partial | A minimally populated model remains usable and visible |
| Ideal | A complete active-model envelope renders the selected model |
| Offline transition | A stalled health request renders `Connecting...`, then the bounded `Cannot connect to server` state |
| Trust boundary | Evil-Origin proxy mutation and percent-encoded Host/Origin aliases are rejected |
| Real inference | The SHA-256-verified 1,185,376-byte `stories260K.gguf` loaded CPU-only, received a prompt through Chat, rendered a non-error assistant response, and unloaded successfully |
| Context lifetime | Cross-thread context leases release safely; unload waits for an active context and then proceeds |
| Metadata transaction | An injected index-commit failure preserves the prior metadata/tensor generation and in-memory index |
| Model accounting | Missing files fail closed and non-empty sub-megabyte files account for one MB |
| Responsive | Dashboard, Models, Chat, Playground, and Settings have no document-level horizontal overflow on desktop Chromium or Pixel 7 |
| Route coverage | Fourteen public routes rendered without page/runtime errors |

## Additional release evidence

The prior isolated QA run also completed Rust format/check/test, reported no
actionable RustSec vulnerabilities (15 documented inherited advisories), and
produced the v1.3.1 MSI and NSIS installers. These package artifacts were not
published.

Windows CI downloads the same test model from an immutable upstream revision,
verifies its SHA-256, and exports `SNAPLLM_E2E_MODEL_PATH`, so the real Chat
journey cannot be silently skipped in the required browser job.

## Verdict

All locally executable acceptance gates are green. External push, tag, release,
and publication remain outside this local QA approval and require explicit
human authorization.
