# Security Policy

## Supported versions

Security fixes are developed on the default branch and included in the next
release. Only the latest released version is supported after a fix ships.

## Reporting a vulnerability

Please do not open a public issue for a suspected vulnerability. Use
[GitHub private vulnerability reporting](https://github.com/snapllm/snapllm/security/advisories/new)
and include:

- the affected version or commit;
- the operating system and build configuration;
- a minimal reproduction;
- the security impact and any known preconditions; and
- whether the issue is already being exploited or publicly discussed.

Do not include real API keys, private model data, or other credentials. We will
acknowledge the report through the advisory and coordinate disclosure after a
fix is available. If private reporting is unavailable, open a public issue that
contains no exploit details and asks a maintainer to establish a private
channel.

## Deployment baseline

SnapLLM is local-first and listens on loopback by default.

- A non-loopback bind requires `SNAPLLM_API_KEY` with 32–4096 visible ASCII
  characters. Supply it only through the process environment.
- Non-loopback binds also require `SNAPLLM_NETWORK_GUARD=reverse-proxy`, which
  asserts that a proxy enforces connection, request-rate, and per-client
  limits. Containers published exclusively on host loopback may instead use
  `loopback-port-publish`; this value is not valid for a public host mapping.
- Authenticate protected routes with `Authorization: Bearer <key>` or
  `X-API-Key: <key>`. The `/` and `/health` endpoints are intentionally public.
- Browser access uses exact Origin allowlisting. Configure additional trusted
  origins with `--cors-origin` or `SNAPLLM_CORS_ORIGINS`; do not expose an
  untrusted origin.
- Requests are subject to Host validation and bounded payloads. Model and
  workspace paths must remain within their configured roots.
- Header and request reads use a short deadline to limit slow-client resource
  exhaustion. Remote deployments still require a protective reverse proxy
  with connection, request-rate, and per-client limits.
- Put TLS and network access controls in front of any remote deployment. The
  built-in HTTP listener does not terminate TLS.
- Run the container as its included unprivileged user, keep model mounts
  read-only, and do not publish the Compose port beyond loopback unless remote
  access is intentional.

API keys protect the SnapLLM listener; they do not make untrusted model files,
prompts, generated content, or host-mounted directories safe. Treat those as
untrusted inputs and grant the process only the filesystem access it needs.

Model-generated tool calls are untrusted data, not authorization. Applications
must allowlist tool names, validate arguments against strict schemas, and
require human approval before consequential, external, privileged, destructive,
or paid actions. Never dispatch generated names or arguments directly to a
shell, SQL interpreter, filesystem primitive, or network client.

## Public disclosure

Please allow maintainers time to investigate and release a fix before public
disclosure. Coordinated disclosure details will be recorded in the private
advisory and, after release, in the changelog or a public security advisory.
