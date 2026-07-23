# Dependency review — 2026-07-23

Scope: direct desktop dependencies reported by `npm audit --omit=dev`.

| Dependency | Decision | Reason | Validation required |
|---|---|---|---|
| `axios` | Upgrade to `^1.18.0` | Current range resolves below fixes for request smuggling, prototype-pollution gadgets, credential leaks, and resource-exhaustion advisories. It is the desktop API transport. | API contract unit tests, TypeScript, production build, clean production audit |
| `react-router-dom` | Migrate to `^7.18.1` | A new audit update marked all v6 releases vulnerable to open redirect/XSS and SSR constructor injection; npm identifies 7.18.1 as the first complete fix. SnapLLM uses the compatible declarative browser APIs and does not use SSR hydration. | Lint, TypeScript, unit tests, production build, all-route Playwright journey, clean full-tree audit |
| `react-syntax-highlighter` | Upgrade to `^16.1.1` | Required to remove the vulnerable PrismJS/Refractor chain. This is a direct, UI-only dependency. | Markdown rendering type check, unit tests, production build |
| `uuid` | Upgrade to `^14.0.1` | Current release is affected by missing output-buffer bounds checks. Usage is limited to generated identifiers. | TypeScript, unit tests, production build |
| `vite`, `@vitejs/plugin-react` | Upgrade to the current compatible major | The existing development server chain has path-traversal, arbitrary-file-read, and cross-origin development-server advisories. | TypeScript, unit tests, production build |
| `esbuild` | Add as an explicit development dependency | Vite 8 externalizes its deprecated transform compatibility path; the existing Vite configuration still invokes that path and otherwise cannot build. | Production build and clean full-tree audit |
| `vitest`, `@vitest/ui` | Upgrade together to `^4.1.10` | The existing UI server is affected by a critical arbitrary file read/execution advisory. The UI package and runner must remain version-aligned. | Unit tests and production build |
| `eslint`, `@eslint/js`, `typescript-eslint`, `globals` | Add current development-only tooling | The repository declares lint as a release gate but did not include the executable, parser, or configuration needed to run it. | Zero-warning lint run and clean full-tree audit |

No replacement package was introduced. Lockfile-only transitive upgrades are accepted when selected by npm for the reviewed direct versions. Production and full-tree audits remain release gates; any advisory without an available non-breaking fix must be recorded with exploitability and compensating controls rather than silently waived.

## Rust/Tauri lockfile review

`cargo-audit` 0.22.2 found seven actionable advisories in the original lock:
`bytes` integer overflow, `crossbeam-epoch` invalid pointer dereference,
two `quick-xml` denial-of-service issues, two `tar` archive issues, and a
`time` stack-exhaustion issue. The lock now resolves `bytes` 1.11.1,
`crossbeam-epoch` 0.9.20, `plist` 1.10.0 / `quick-xml` 0.41.0, `tar` 0.4.45,
`time` 0.3.47, and `anyhow` 1.0.104. A fresh audit reports zero
vulnerabilities.

RustSec still reports maintenance/unsoundness warnings inherited through the
Tauri 1 platform graph (principally GTK3 on Linux plus legacy `rand` versions).
Those are not reported as actionable vulnerabilities by `cargo-audit`; removing
them requires a separately governed Tauri 2 migration. CI and release install
the pinned auditor and fail on any actionable advisory.

## Playwright UI regression harness

`@playwright/test` 1.61.1 is accepted as an exact, development-only dependency.
Need: reproducible browser journeys, responsive-layout checks, retained traces,
and video/screenshots on failure cannot be replaced by a small owned helper.
Health: the npm registry reports a release on 2026-07-23 and the package is the
maintained Playwright test runner. Weight: three development packages plus a
separately installed Chromium test binary; none ship in the desktop bundle.
License: Apache-2.0. Security: installation completed with `npm audit` reporting
zero vulnerabilities. The exact version and transitive graph are locked, and
the suite runs through `npm run test:e2e`.

## CI model fixture

The Windows browser gate downloads `stories260K.gguf` (1,185,376 bytes) from
the official `ggml-org/tiny-llamas` repository at immutable revision
`6e091d820cbe8f22eeb604d136403eca290b8c1e`. The workflow verifies SHA-256
`047bf46455a544931cff6fef14d7910154c56afbc23ab1c5e56a72e69912c04b`
before use. It is a test-only model artifact, is not committed or packaged, and
makes real model loading and Chat generation mandatory in CI.
