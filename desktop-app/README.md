# SnapLLM Desktop App

React/Vite frontend and Tauri desktop shell for the SnapLLM C++ HTTP server.
The application does not start or embed a Python/FastAPI backend.

## Prerequisites

- Node.js 20.19 or newer
- npm
- A built SnapLLM server for API-backed features
- Rust and the platform-specific Tauri prerequisites for native desktop builds

## Development

Start SnapLLM on its default loopback listener:

```bash
../build_cpu/bin/snapllm --server --host 127.0.0.1 --port 6930 \
  --cors-origin http://localhost:9780 \
  --cors-origin http://127.0.0.1:9780
```

Then install dependencies and start the web development server:

```bash
npm install
npm run dev
```

Vite serves the UI at `http://localhost:9780` and proxies API requests to the
loopback server. Production browser builds use the origin that served the UI;
native Tauri builds default to `http://localhost:6930`.

To use a different SnapLLM endpoint, create `.env.local`:

```dotenv
VITE_API_URL=http://127.0.0.1:6930
```

For a native development window:

```bash
npm run tauri:dev
```

## API authentication

The default loopback SnapLLM listener can run without a key. A non-loopback
listener requires the server process to receive `SNAPLLM_API_KEY` with 32–4096
visible ASCII characters.

Enter that same value in the desktop Settings page. It is held in renderer
memory for the current session and sent as a Bearer token; it is not written to
local storage or the server configuration. Restarting the app clears it.

Browser access is also subject to the server's exact Origin allowlist. If the
UI is served from a custom origin, start SnapLLM with a matching repeatable
`--cors-origin` value or include it in `SNAPLLM_CORS_ORIGINS`.

## Available pages

Routes currently wired in `src/App.tsx` are:

- Dashboard
- Chat, Images, and Vision
- Models, Compare, and Quick Switch
- Contexts
- Playground, Batch Processing, and Metrics
- Settings, About, and Help

Team administration, audit logs, SSO/RBAC, persistent API-key management, and
security-score dashboards are not implemented.

## Scripts

```bash
npm run dev          # Vite development server
npm run build        # Type-check and build web assets
npm run preview      # Preview the production web bundle
npm run lint         # ESLint
npm run test         # Vitest
npm run tauri:dev    # Native development window
npm run tauri:build  # Native installer/bundle
```

Live API browser tests can start a local daemon automatically by setting
`SNAPLLM_E2E_API_COMMAND` to a trusted local command. For example:

```powershell
$env:SNAPLLM_E2E_API_COMMAND = '.\\build_core\\bin\\snapllm.exe --server --host 127.0.0.1 --port 6930 --load-model lfm D:\\Models\\LFM2.5-1.2B-Instruct-Q5_K_M.gguf --gpu-layers 0'
npm run test:e2e
```

Without that variable, offline and mocked UI tests remain deterministic and
live API tests fail clearly on the missing daemon instead of silently using a
different service.

## Security boundaries

The Tauri shell uses an explicit Content Security Policy and a narrow Tauri 2
capability set. Filesystem operations are limited to the configured
model/workspace scopes in `src-tauri/capabilities/default.json`. Selecting a
file in the UI does not relax the SnapLLM server's own path
confinement: API-supplied paths must still remain within configured roots.

Do not put API keys in `VITE_*` variables; Vite embeds those values into client
assets.

## Validation

Before submitting desktop changes, run:

```bash
npm run lint
npm test -- --run
npm run build
```

Native packaging additionally requires:

```bash
npm run tauri:build
```

See [DOCUMENTATION.md](DOCUMENTATION.md) for the source layout and runtime
contract.
