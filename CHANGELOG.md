# Changelog

## 1.17.35

- Generate a strong runtime API key automatically for non-loopback servers,
  bootstrap it for loopback-only Docker UI clients, and remove the need for
  users to invent a random key during local setup.

## 1.17.34

- Show authentication-specific model loading errors and provide a retry
  action instead of a generic “Unable to load models” state.

## 1.17.33

- Add restart-required controls to Server Settings and continuously refresh
  model-path configuration on the Models page.

## 1.17.31

- Report HTTP 401 responses as authentication-required instead of generic
  server-offline errors in the UI.

## 1.17.30

- Keep generated Docker configuration out of source control while preserving
  the writable config mount directory.

## 1.17.29

- Mount a writable Docker config volume so Server Settings changes persist
  instead of failing on the read-only application filesystem.

## 1.17.28

- Persist Server Settings drafts locally and make offline/authenticated save
  failures actionable instead of silently disabling the save control.

## 1.17.27

- Make Docker host model mounting configurable and clarify container-visible
  model paths in Server Settings.
- Distinguish authentication failures from an offline daemon in Settings.

## 1.17.26

- Fix hardened Docker UI startup by bypassing nginx cache chown entrypoint
  behavior and granting only the capabilities required for worker startup.

## 1.17.25

- Wire the Metrics usage refresh control to live API queries.

## 1.17.24

- Wire Batch Processing import with JSON validation and prompt hydration.

## 1.17.23

- Wire Metrics refresh/export/alert controls and model reload/configure actions
  to live data and runtime configuration.

## 1.17.22

- Fix context demotion persistence, tier accounting, stale cache cleanup, and
  visible UI errors when a demotion request is rejected.

## Unreleased

- Keep live UI queries polling after transient API errors so recovered daemon
  state and metrics reconcile without a manual refresh.

- Add a Docker Compose UI service that serves the Dashboard on port 9780 and
  proxies API and streaming routes to the SnapLLM container.

- Disable `GGML_NATIVE` in Docker builds so CPU images compiled on CI runners
  do not emit host-specific instructions such as AMX that crash on other
  machines.

- Fix Docker/Linux model loading by composing per-model workspace paths with
  platform-native separators instead of Windows-only backslashes.

- Normalize Docker Hub usernames before constructing image references so
  whitespace or uppercase account input cannot produce invalid tags.

- Add a CUDA 12.6 Docker image and publish both CPU and CUDA variants through
  the protected Docker Hub release workflow.

- Add a tag-triggered, credential-gated Docker Hub publisher with versioned
  CPU images, OCI metadata, and a container health check.

- Correct the development offline banner to show the daemon endpoint on port
  6930 instead of the Vite UI proxy port 9780.

- Add bounded Desktop daemon supervision and restart-on-failure login-task
  settings so the local API recovers from unexpected process exits.

- Prevent Chat from posting stale model ids after a model unload or switch;
  the send controls now follow the latest loaded-model snapshot.

- Make OpenAI, Anthropic, single-generation, and batch requests resolve the
  requested model without mutating the server-wide active-model selection.
- Add `SNAPLLM_MAX_ACTIVE_INFERENCES` as a bounded opt-in for deployments with
  sufficient VRAM; the safe serialized default remains unchanged.
- Propagate cached-context streaming cancellation when an SSE client
  disconnects, so inference gates are released promptly.
- Add thread-safe prefetch transition learning, deterministic predictions, and
  cache hit-rate accounting without claiming unsupported tensor loads.
- Make `/v1/chat/completions` streaming by default; clients can preserve the
  buffered response with `"stream": false`.
- Add a Settings control for live-tunable simultaneous inference slots,
  bounded by the configured HTTP worker count; the safe default remains 1.
- Harden the throughput benchmark with health/scheduler preflight checks,
  explicit CPU/GPU mode labels, and clear non-zero failure behavior when the
  daemon or model is unavailable.
- Fix release version-consistency validation for the Tauri 2 root-level
  `version` field.
- Add opt-in, hardware-independent GPU recovery failure hooks and regression
  coverage so rebalance/reload failures are verified as fail-closed results.
- Verify the rebuilt CPU-mode CLI through Playwright: model registry placement,
  Chat UI inference, and CORS wiring now pass in one live test.
- Align standalone Windows/Linux release packagers with CI: CPU is the default
  archive mode, with `cpu|gpu` as the user-facing choice and `cuda` retained
  as a compatibility alias.
- Align Quickstart archive examples and GPU build guidance with the public
  `cpu|gpu` packaging terminology.
- Include the built web UI in standalone release packages and wire generated
  start scripts to `--ui-dir ui`, matching CI-produced artifacts.
- Align HTTP security integration with the API's structured validation contract:
  malformed token limits may return 400 or 422, both fail closed before
  inference.
- Fix Windows standalone package launcher generation by moving model path/name
  validation into a packaged PowerShell script; batch generation no longer
  breaks on command metacharacters.
- Complete Windows CPU/GPU package dry runs: both archives now include the
  executable, UI, version file, launchers, and mode-correct CUDA DLL set.
- Update the manual release workflow example to the current `v1.17.8` version
  line so operators do not start a release from the retired `v1.3.1` example.
- Update the HTTP security harness for structured Messages validation while
  retaining strict authentication, origin, host, and public-bind assertions.
- Add the model-switching benchmark as a CMake target and record resident,
  warm-cache, and unresolved cold-load results in QA documentation.
- Isolate cold-load lifecycle benchmarks from rapid-switch state and use
  explicit CPU-only placement for deterministic recovery measurements.
- Treat an absent vDPE manifest as an expected informational state when
  manifest generation is disabled, instead of emitting a false error.
- Migrate the desktop shell from Tauri 1 to Tauri 2.11.5 and regenerate the
  Rust dependency lockfile, removing the obsolete rand 0.7 chain.
- Complete the Tauri 2 frontend/runtime wiring: move the API package to runtime
  dependencies, use plugin dialog/filesystem APIs, add scoped capabilities, and
  align Linux CI with the libsoup3/WebKitGTK 4.1 backend.
- Add `$HOME/Models` to the desktop filesystem capability for Linux/macOS model
  discovery and split the Vite bundle into bounded cacheable chunks.
- Lazy-load syntax highlighting so the large grammar bundle is fetched only
  when a response contains a code block.
- Allow Playwright live API tests to start an explicitly configured local
  daemon through `SNAPLLM_E2E_API_COMMAND`; offline tests remain deterministic.
- Align push CI with the Tauri 2 Linux dependency stack (`libsoup3`/WebKitGTK
  4.1) so regular CI and release builds use the same native prerequisites.
- Harden the Playwright loop against stale Vite processes and make live API
  checks opt-in, preventing offline runs from spawning long-lived retry storms.
- Remove the duplicate React Query provider and stop polling failed local API
  queries until the daemon reconnects, preventing offline retry storms.
- Apply the same stop-on-error polling policy to page-level metrics, models,
  contexts, chat, vision, comparison, batch, and playground queries.
- Add the first bounded scheduler slice: least-loaded compatible model routing
  with latency/tie rotation, per-model in-flight gauges, waiting admission
  metrics, and Metrics UI visibility. Explicit model requests remain pinned.
- Apply the same load-aware scheduler to Anthropic Messages, text generation,
  and batch generation endpoints.
- Run and record live low-weight concurrent benchmarks: 16/16 requests
  completed at client concurrency 4, 8, and 16 with bounded admission.
- Add benchmark preflight and explicit CPU/GPU run labels so utilization and
  scheduler settings cannot be confused between runs.

## [1.17.8] - 2026-07-25

- Fix the Tauri resource-directory API call so the desktop Rust check compiles.

## [1.17.7] - 2026-07-25

- Synchronize the desktop Cargo lockfile root package version with the release.

## [1.17.6] - 2026-07-25

- Add an accessible name to the sidebar collapse control.
- Add a browser regression check for unnamed icon-only buttons.

## [1.17.5] - 2026-07-25

- Wire Metrics and API Reference into the sidebar and command palette.
- Move the API Reference UI route to `/docs/api` to avoid the `/api` dev proxy namespace.
- Add browser coverage for every registered navigation target.

## [1.17.4] - 2026-07-25

- Fix TypeScript narrowing in the desktop Tauri runtime detection path.

## [1.17.3] - 2026-07-25

- Wire SnapLLM Desktop to start, stop, and inspect the native local daemon.
- Start the daemon automatically in the packaged desktop app when the CLI is available.
- Replace stale server/FastAPI launch guidance with the daemon command.

## [1.17.2] - 2026-07-25

- Pin the desktop Rust lockfile to the published `bstr 1.13.0` release.

## [1.17.1] - 2026-07-25

- Repair the npm lockfile for Windows/Linux optional native dependency
  resolution so `npm ci` is reproducible in Actions.

## [1.16.0] - 2026-07-25

- Add deterministic production request routing with wrong-modality rejection.
- Add GPU pressure rebalancing and model recovery APIs.
- Add opt-in end-to-end concurrent HTTP throughput benchmark.
- Add regression coverage for routing and cleanup-safe bounded concurrency.

## [1.17.0] - 2026-07-25

- Add cross-platform user-level daemon start, stop, and status commands.
- Add Windows Scheduled Task, Linux systemd-user, and macOS launchd templates.

## [1.15.0] - 2026-07-25

- Improve inference burst handling with bounded HTTP backpressure and request-budget queue waits.

## [1.14.0] - 2026-07-25

- Add a narrowly scoped React Router RSC audit exception and pin `brace-expansion` to 5.0.8.

## [1.13.0] - 2026-07-25

- Pin the Tauri lockfile to the portable published `plist 1.10.0` release.

## [1.12.0] - 2026-07-25

- Fix all SnapLLM-owned instances of the CodeQL integer-multiplication-cast warning.

## [1.11.0] - 2026-07-25

- Configure CodeQL to scan SnapLLM product code while excluding vendored third-party dependencies.

## [1.10.0] - 2026-07-25

- Prevent 32-bit intermediate overflow in GPT-J context-size calculations.

## [1.9.0] - 2026-07-24

- Update `serde_with`, `tar`, and `rand` to their patched security releases.

## [1.8.0] - 2026-07-24

- Add WebKitGTK 4.0 linker-name compatibility aliases for Tauri 1.x on Ubuntu 24.04.

## [1.7.0] - 2026-07-24

- Pin the portable Cargo lockfile to the published autocfg 1.5.0 release.

## [1.6.0] - 2026-07-24

- Update the locked Wry runtime to 0.24.12, which correctly imports WebKitGTK settings extension traits.

## [1.5.0] - 2026-07-24

- Enable WebKitGTK v2.36 APIs required by the Tauri desktop runtime on current Linux runners.

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

### Release preparation

- Bump the canonical release version to 1.17.9 after the 03881fb CI and CodeQL gates passed.

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
