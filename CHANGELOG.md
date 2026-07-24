# Changelog

## [1.4.0] - 2026-07-24

- Use Ubuntu 24.04 WebKitGTK 4.1 libraries with Tauri 1.x-compatible pkg-config aliases in desktop CI.

## [1.3.9] - 2026-07-24

- Run the Tauri desktop CI job on Ubuntu 22.04, which provides the WebKitGTK 4.0 development packages required by Tauri 1.x.

## [1.3.8] - 2026-07-24

- Fix Linux desktop CI by installing the JavaScriptCore GTK development package required by Tauri.

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.3.7] - 2026-07-23

### Fixed

- Ubuntu Tauri CI now installs the libsoup 2 development package required by
  the Tauri 1 WebKit bindings.

## [1.3.6] - 2026-07-23

### Fixed

- Preserve Linux optional npm dependency entries in the committed lockfile so
  GitHub Ubuntu `npm ci` remains reproducible.

## [1.3.5] - 2026-07-23

### Fixed

- CI browser health assertions now validate the API contract without pinning an
  obsolete patch version.
- Tauri Cargo metadata and lockfile versions are synchronized.

## [1.3.4] - 2026-07-23

### Fixed

- Ubuntu 24.04 Tauri CI now installs the available WebKitGTK 4.1 development package.

## [1.3.3] - 2026-07-23

### Fixed

- Cross-platform path regression tests now reject Windows UNC/device paths on
  POSIX hosts.
- CI uses the Windows 2022 runner that provides the Visual Studio generator.
- The npm lockfile now includes Linux optional `@emnapi` packages required by
  `npm ci`.

## [1.3.2] - 2026-07-23

### Fixed

- Web UI launchers now open Vite on `localhost:9780`; the API remains on
  `localhost:6930`, preventing the API-only root from being mistaken for the UI.

## [1.3.1] - 2026-07-23

### Added

- Runtime API-key authentication for protected HTTP routes.
- Strict Host validation and exact browser Origin allowlisting.
- Request size, count, token, image, and path boundary validation.
- Integrity and bounds checks for persisted context cache files.
- Security policy and regression coverage for the public HTTP trust boundary.
- CI and release gates for Rust tests, RustSec advisories, version consistency,
  and packaged-binary smoke checks.
- Desktop/server contract coverage for context-detail response unwrapping.
- Playwright desktop/mobile route, proxy, model-selection, and responsive-layout
  regression journeys with retained failure artifacts, five-state UI coverage,
  and a real low-weight model Chat response.

### Changed

- The server now binds to loopback by default.
- Non-loopback listeners require `SNAPLLM_API_KEY` containing 32–4096 visible
  ASCII characters.
- API keys are accepted from the environment only and are neither persisted nor
  exposed through configuration responses.
- Browser launch no longer invokes a command shell.
- Model and workspace paths are canonicalized and confined to configured roots.
- Docker runs as an unprivileged user; the Compose service uses a read-only root
  filesystem, drops capabilities, and requires an API key.
- Performance documentation now distinguishes constant-time indexed lookup from
  workload-dependent model switching and generation.
- Configuration changes are persisted transactionally and applied on restart,
  preventing rejected updates from mutating live server state.
- Desktop version labels are compiled from the package version.
- Context deduplication identities now use SHA-256 rather than CRC32.
- GitHub Actions are commit-pinned and release jobs verify signed tags.

### Fixed

- Model unload and reload lifecycle accounting.
- Context save/query synchronization and cache persistence failure handling.
- Cross-thread KV context ownership now blocks model unload only until active
  contexts are released, without transferring thread-affine mutex ownership.
- Workspace model metadata and tensor catalogs now switch generations through
  one atomic index commit and roll back cleanly on persistence failure.
- Model size accounting now fails closed when a GGUF cannot be inspected and
  rounds non-empty sub-megabyte models into the VRAM budget.
- Desktop API request shapes, React Query wiring, and runtime API-key headers.
- React Router migrated to 7.18.1 after new open-redirect/XSS and SSR
  constructor-injection advisories covered every v6 release.
- Duplicate server-side model loading during startup.
- Context tier accounting during cold-cache restoration and promotion.
- Release lifecycle tests now remain active in optimized builds.
- Desktop development requests now use the Vite proxy, including Playground
  requests, while protected generated images are fetched with session auth.
- Browser builds now use their serving origin, native builds retain the
  loopback fallback, and health failures resolve to an offline state within a
  bounded timeout.
- CPU/automatic/full-GPU model load choices now map to the server's actual
  `n_gpu_layers` configuration.
- Renderer-owned image blob URLs are revoked on deletion, cancellation, and
  unmount.
- Empty metrics data now renders a valid placeholder ring instead of an SVG
  path containing `NaN`.
- The cache-clear route now returns explicit `501` instead of claiming a no-op
  succeeded, and startup output labels unsupported routes accurately.

### Security

- Closed unauthenticated non-loopback API exposure.
- Replaced permissive/reflected CORS behavior with an exact allowlist.
- Rejected forged Host headers, path traversal, malformed cache files, and
  oversized or invalid request parameters.
- Removed shell-command construction from browser launching and detailed
  exception disclosure from client responses.
- Bounded pre-authentication request receipt, directory scans, persisted JSON,
  cache reads, and decompression allocations.
- Bounded and finite sampling parameters, transactional concurrent config
  writes, shortened slow-client deadlines, and an explicit reverse-proxy
  or loopback-only container-port guard for non-loopback binds.
- Removed prompt excerpts from logs and rejected unsupported Messages image
  blocks instead of fabricating textual image handling.
- Replaced the Windows model launcher with validated PowerShell argument passing.
- Removed renderer access to recursive workspace deletion.
- Disabled incomplete direct dequantization entry points that returned fabricated
  metadata or zero-filled tensors.
- Preserved browser Origin through the development proxy and added a
  cross-origin mutation regression.
- Preserved raw HTTP header values at the vendored parser boundary so
  percent-encoded Host/Origin aliases cannot satisfy exact trust checks.
- Bounded authenticated image/frame file responses and strictly validated
  base64 image input.
- Sanitized renderer error logging so Axios request credentials cannot be
  exposed.
- Replaced workspace metadata substring parsing and truncating writes with
  typed JSON validation and atomic replacement.
- Made inference-slot release exception-safe and changed unsupported cache
  library/binding surfaces to fail explicitly.
- Made the real Chat inference journey mandatory in Windows CI with a
  revision-pinned, SHA-256-verified 1.2 MB model fixture.
- Removed raw inference-context and tensor pointers from the public bridge API.
