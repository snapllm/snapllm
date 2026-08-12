# SnapLLM Desktop Architecture

This document describes the code that is currently wired. It intentionally does
not list mock pages or planned enterprise features as shipped capabilities.

## Runtime topology

```text
React UI (browser or Tauri webview)
        |
        | HTTP/SSE, optional Bearer API key
        v
SnapLLM C++ server on http://127.0.0.1:6930
        |
        +-- model manager and llama.cpp
        +-- context/KV cache manager
        +-- optional vision and diffusion backends
```

There is no Python/FastAPI service in this topology.

## Frontend stack

- React 18 and TypeScript
- Vite
- React Router
- TanStack Query
- Zustand
- Axios plus `fetch` for streaming requests
- Tailwind CSS
- Tauri 2 for the native shell

## Source layout

```text
desktop-app/
|-- src/
|   |-- App.tsx              # Providers and the authoritative route list
|   |-- components/          # Layout and reusable UI
|   |-- hooks/useApi.ts      # TanStack Query wrappers
|   |-- lib/api.ts           # HTTP client, DTOs, streaming, Tauri file helpers
|   |-- pages/               # Routed product pages
|   `-- stores/              # Renderer state
|-- src-tauri/
|   |-- src/main.rs          # Native entry point
|   `-- tauri.conf.json      # CSP, capabilities, packaging, and window config
|-- package.json
`-- vite.config.ts
```

## Routes

`src/App.tsx` is the source of truth.

| Route | Page |
|---|---|
| `/` | Dashboard |
| `/chat` | Chat |
| `/images` | Image generation |
| `/vision` | Vision |
| `/models` | Model management |
| `/compare` | Model comparison |
| `/switch` | Loaded-model selection |
| `/contexts` | Context/KV cache management |
| `/playground` | API playground |
| `/batch` | Batch processing |
| `/metrics` | Runtime metrics |
| `/settings` | API endpoint and session key |
| `/about` | About |
| `/help` | Help |

There are no wired routes for Team, Audit, SSO/RBAC, security scoring, or
persistent API-key administration.

## API client contract

`src/lib/api.ts` defaults to `http://localhost:6930`; `VITE_API_URL` can replace
that endpoint at build/dev time.

The server permits an unauthenticated loopback deployment. For a non-loopback
bind:

1. Set `SNAPLLM_API_KEY` in the server process to 32–4096 visible ASCII
   characters.
2. Enter the same value in Settings.
3. Configure the UI's exact browser origin on the server with `--cors-origin`
   or `SNAPLLM_CORS_ORIGINS`. Vite development needs both
   `http://localhost:9780` and `http://127.0.0.1:9780`.

The UI keeps the key in module memory and adds `Authorization: Bearer <key>` to
Axios and streaming requests. It does not persist the key. Clearing it or
restarting the renderer removes it.

Never place secrets in `VITE_*` environment variables because Vite exposes
those values to client code.

## Model and context operations

The frontend delegates model loading, selection, unloading, generation,
vision, diffusion, and context management to the C++ server. Selecting a model
that remains resident avoids a weight reload, but the UI makes no specific
end-to-end latency guarantee.

Context lookup uses the server's index, while query generation remains
dependent on the model, cached prefix, query length, requested output, and
hardware.

Model/workspace paths selected through Tauri remain subject to two independent
controls:

- the Tauri 2 capability filesystem scope in
  `src-tauri/capabilities/default.json`; and
- the server's canonical-path confinement to configured roots.

## Tauri security

Tauri 2 capabilities disable implicit renderer IPC access, enable only the file
dialog and required filesystem operations, scope those operations to model and
workspace directories, and define a Content Security Policy in
`src-tauri/tauri.conf.json`. Keep these scopes narrow when adding features.

The renderer connects only to the local SnapLLM HTTP endpoint permitted by the
CSP. Remote server support would require an explicit CSP and trust-boundary
review.

## Development and verification

```bash
npm install
npm run dev
```

Run the frontend checks before submitting changes:

```bash
npm run lint
npm test -- --run
npm run build
```

For the native shell:

```bash
npm run tauri:dev
npm run tauri:build
```

Tauri commands require Rust and the operating system's native Tauri
prerequisites.
