# Live UI/API E2E Loop — 2026-08-11

## Scope

QA verification of the desktop Vite UI and optional live SnapLLM daemon using
Playwright, including a low-weight model at `D:\Models\LFM2.5-1.2B-Instruct-Q5_K_M.gguf`.

## Commands and evidence

Offline/default run from `desktop-app`:

```text
npm run test:e2e -- --project=desktop-chromium
```

Observed output after the harness fix:

```text
Running 8 tests using 1 worker
2 skipped
6 passed (32.4s)
```

The live proxy and model-generation checks are explicitly skipped unless
`SNAPLLM_E2E_API_COMMAND` is set. Route rendering, mocked model states,
stalled-health/offline behavior, accessibility, and responsive layout run
without a daemon. The Vite server is started fresh for each run so stale
processes cannot cause later `ERR_CONNECTION_REFUSED` failures. The proxy
assertion accepts either 400 (payload validation) or 403 (origin enforcement),
both valid rejection outcomes for the malformed request.

Live run attempted with:

```powershell
$env:SNAPLLM_E2E_API_COMMAND='D:\Mass\BackUps\GoldMine\GPU_CPU_modes\SnapLLM-Codex\SnapLLM\build_core\bin\snapllm.exe --server --host 127.0.0.1 --port 6930 --gpu-layers 0 --load-model low-weight-ui-e2e D:\Models\LFM2.5-1.2B-Instruct-Q5_K_M.gguf'
$env:SNAPLLM_E2E_MODEL_PATH='D:\Models\LFM2.5-1.2B-Instruct-Q5_K_M.gguf'
npm run test:e2e
```

The low-weight model load did not become ready within the command window and
the run was stopped; no live inference result is claimed. A direct daemon
startup reached model-loading output but did not expose `/health` before the
process was terminated:

```text
[SnapLLM] HTTP inference gate: max 1 concurrent
[SnapLLM] Loading model: low-weight-ui-e2e
Loading model: low-weight-ui-e2e
```

## QA disposition

Offline/UI harness: pass (6 passed, 2 intentional skips). Live API-backed
flows remain opt-in and require a healthy daemon; no live inference success is
claimed by the offline run.

## Follow-up verification (2026-08-12)

The earlier live 401 responses were traced to a stale process already listening
on port 6930. It had been started with a temporary `SNAPLLM_API_KEY`, and
Playwright correctly reused that process. After stopping it, the documented
live command was rerun from `desktop-app` with a clean environment:

```text
Running 8 tests using 1 worker
7 passed, 1 skipped (51.8s)
```

Health/proxy, model lifecycle, navigation, offline-state, and responsive checks
passed. The low-weight inference test remains intentionally skipped by its
fixture; no inference success is claimed. A separate clean-environment smoke
check returned HTTP 200 for both `/health` and `/api/v1/models` without an API
key on loopback.

## Restart-recovery verification (2026-08-12)

After clearing stale Playwright/Vite processes left by an interrupted run, the
desktop suite was rerun with a 45-second per-test bound:

```text
Running 8 tests using 1 worker
2 skipped
6 passed (53.0s)
```

The two skips are intentional live/model fixture skips. No test hung or
crashed; the earlier system restart was consistent with orphaned Node test
processes from the interrupted run, not a reproducible application crash.

The runtime retry loop was also hardened: the app now has one React Query
provider, failed health/cache/context queries stop polling, and the global
query policy does not retry disconnected local requests.

Page-level polling was audited and normalized as well; all periodic local API
queries now stop after an error rather than continuing background requests.
