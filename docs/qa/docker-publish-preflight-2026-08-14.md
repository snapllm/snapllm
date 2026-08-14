# Docker publishing preflight — 2026-08-14

Prepared the Docker Hub workflow at `.github/workflows/docker-publish.yml`.
It publishes only from deliberate `vX.Y.Z` tags or an explicitly selected
existing tag through `workflow_dispatch`, and requires the protected
`docker-publish` environment with `DOCKERHUB_USERNAME` and `DOCKERHUB_TOKEN`.

Local validation:

- `node scripts/check_versions.mjs` — passed (`1.17.13`)
- PyYAML parsing of workflow and Compose files — passed
- CPU `docker build` — passed after starting Docker Desktop; the image reached
  a healthy container state on the mapped test port.
- CUDA base-image/GPU passthrough probe — not completed within the local pull
  window; `nvidia/cuda:12.6.3` must be pulled and tested with `--gpus all`.

The default image is CPU-only. The CUDA workflow image is built from the
NVIDIA CUDA 12.6 devel/runtime images and is tagged with `-cuda`/`cuda`.
