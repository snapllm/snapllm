# Docker publishing preflight — 2026-08-14

Prepared the Docker Hub workflow at `.github/workflows/docker-publish.yml`.
It publishes only from deliberate `vX.Y.Z` tags or an explicitly selected
existing tag through `workflow_dispatch`, and requires the protected
`docker-publish` environment with `DOCKERHUB_USERNAME` and `DOCKERHUB_TOKEN`.

Local validation:

- `node scripts/check_versions.mjs` — passed (`1.17.13`)
- PyYAML parsing of workflow and Compose files — passed
- `docker build` — not runnable here because Docker Desktop's Linux engine is
  unavailable (`dockerDesktopLinuxEngine` named pipe missing).

The default image is CPU-only. CUDA publishing needs a separate CUDA base
image and NVIDIA Container Toolkit validation; it is intentionally not claimed
by this workflow.
