# ADR-001: Secure local and network API trust boundary

**Date:** 2026-07-23 · **Status:** accepted
**Deciders:** Chief Architect · CISO advisory co-sign · human scope authorization through remediation request

## Context

SnapLLM treats localhost as trusted while reflecting arbitrary browser origins, accepting unauthenticated administrative requests, persisting attacker-controlled bind configuration, opening URLs through a shell, and accepting unrestricted filesystem paths. The same binary can bind publicly and the Docker image does so by default. The desktop and OpenAI-compatible clients still require a usable local-development contract.

## Decision

We will make `/health` and static UI assets public and protect API operations at middleware before dispatch. A non-loopback bind requires a caller-supplied API key of at least 32 bytes from `SNAPLLM_API_KEY`; secrets are never accepted on command lines, persisted, or returned. Protected requests accept `Authorization: Bearer` or `X-API-Key` using constant-time comparison. Loopback may omit a key for CLI compatibility, but Host is validated and browser requests are accepted only from the same server origin, Tauri's application origin, or an explicit `SNAPLLM_CORS_ORIGINS` allowlist. CORS never reflects arbitrary origins or enables credentials. Browser opening uses platform process APIs without a command shell. Model and scan paths are canonicalized and confined to configured workspace/model roots; explicit imports copy files into those roots.

## Alternatives considered

| Option | Pros | Cons | Why not |
|---|---|---|---|
| Chosen: fail-closed public bind, optional loopback key plus strict browser boundary | Secure public default; preserves local CLI usability; standard client auth | Requires UI/key wiring and migration documentation | — |
| Always require a generated key | Strong uniform boundary | Packaged UI needs a secure token handoff mechanism not currently present; generated secret display/storage adds new risk | Revisit if native launcher owns server lifecycle |
| CORS-only protection | Small change | CORS is not authentication; simple requests and non-browser callers remain privileged | Does not close the exposed-server threat |
| Keep proxy-only authentication guidance | No compatibility change | Unsafe defaults remain public and operators can reasonably miss the proxy requirement | Rejected |

## Consequences

- Positive: browser CSRF/rebinding, unauthenticated public control, stored shell injection, and unrestricted filesystem access fail closed.
- Positive: OpenAI-compatible clients can use the conventional Bearer token contract.
- Negative: publicly bound deployments must configure a key and some existing scripts require updates.
- Negative: arbitrary external model paths must be imported or placed under configured roots.
- Invalidation triggers: a native supervisor securely provisions per-session capabilities, or SnapLLM adopts a multi-user identity/authorization model.
