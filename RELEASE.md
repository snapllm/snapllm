# Release Checklist (Public/Release-Grade)

Use this checklist for tagged releases and public distributions.

## Preflight
- Confirm submodules are synced: `git submodule update --init --recursive`
- Verify clean working tree (no local build artifacts)
- Ensure version is updated (see `CMakeLists.txt` project version)

## Build + Smoke Test
### Windows CPU
- `build_cpu.bat`
- Smoke test: `scripts\ci_smoke_test.ps1 -SnapllmPath build_cpu\bin\snapllm.exe`

### Windows GPU (if shipping GPU build)
- `build_gpu.bat`
- Smoke test: `scripts\ci_smoke_test.ps1 -SnapllmPath build_gpu\bin\snapllm.exe`

### Linux CPU
- `./build.sh cpu`
- Smoke test: `scripts/ci_smoke_test.sh build_cpu/bin/snapllm`

### Linux GPU (if shipping GPU build)
- `./build.sh gpu`
- Smoke test: `scripts/ci_smoke_test.sh build_gpu/bin/snapllm`

### Optional Model Load/Switch Smoke Test
Requires local model files (not included in the repo):

```bash
export SNAPLLM_MODEL1_ID=llama3
export SNAPLLM_MODEL1_PATH=/path/to/model1.gguf
export SNAPLLM_MODEL2_ID=gemma
export SNAPLLM_MODEL2_PATH=/path/to/model2.gguf
scripts/model_smoke_test.sh build_cpu/bin/snapllm
```

### Desktop App
- `cd desktop-app`
- `npm ci`
- `npm run build`

### Python Bindings (optional)
- `cmake -S . -B build_cpu -DSNAPLLM_ENABLE_PYTHON_BINDINGS=ON -DSNAPLLM_CUDA=OFF`
- `cmake --build build_cpu`
- `python -c "import sys; sys.path.insert(0, 'build_cpu/python'); import snapllm_bindings"`

## Packaging
- Use `package_release.bat` (Windows) or `package_release.sh` (Linux)
- Verify output package contents match expected targets

## Release Notes
- Summarize new features and breaking changes
- Provide upgrade notes and known issues

## Final Verification
- Run `/health` check on the packaged binaries
- Confirm no large model files are included
