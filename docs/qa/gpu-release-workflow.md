# GPU release workflow

The standard release workflow publishes CPU artifacts for Windows x64, Linux x64,
and macOS arm64. CUDA artifacts cannot be built or runtime-tested on GitHub-hosted
runners because those runners do not expose NVIDIA GPUs.

`.github/workflows/release-gpu.yml` adds explicit manual GPU jobs for Windows and
Linux. They require self-hosted runners with the labels `gpu` and `cuda`, plus a
working CUDA toolkit. The jobs build with `SNAPLLM_CUDA=ON`, package with the
repository packagers, and upload the GPU archive to an existing release tag.

macOS is intentionally not included: CUDA is not supported on Apple GPUs. A future
Metal backend would require a separate implementation and artifact format.
