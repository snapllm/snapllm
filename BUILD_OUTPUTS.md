# Build Outputs and Artifacts

This document lists directories and files that are generated during builds and should not be committed to the repo.

## Core Builds (CMake)
- `build_cpu/` CPU-only build output
- `build_gpu/` GPU build output
- `build_test/` Temporary or experimental build output
- `build/` Generic build output
- `bin/` Local runtime binaries if produced outside of build directories

## Desktop App
- `desktop-app/node_modules/` Dependencies
- `desktop-app/dist/` Production build output
- `desktop-app/src-tauri/target/` Rust build output
- `desktop-app/out/` Static export output (if used)

## Python Bindings
- `build_cpu/python/` or `build_gpu/python/` pybind11 module output

## Models and Workspaces
- `*.gguf`, `*.ggml`, `*.safetensors`, `*.ckpt`, `*.bin`, `*.pt`, `*.pth`
- `SnapLLM_Workspace/` or `*_Workspace/`