# SnapLLM Python Bindings

Python bindings for SnapLLM C++ core using **pybind11**.

## Overview

These bindings expose the C++ `ModelManager` class to Python, allowing direct use of the high-performance SnapLLM inference engine from Python.

**Scope:** The current bindings are **LLM-only**. Diffusion and multimodal (vision) APIs are not exposed in the Python module yet.

## Prerequisites

- Python 3.9+ (with development headers)
- CMake 3.18+
- A SnapLLM build (CPU or GPU)

`pybind11` is fetched automatically during the build if not found on the system.

## Build (In-Tree)

Build bindings from the repo root by enabling the CMake option:

```bash
# CPU-only example
cmake -S . -B build_cpu -DSNAPLLM_ENABLE_PYTHON_BINDINGS=ON -DSNAPLLM_CUDA=OFF
cmake --build build_cpu

# The module will be emitted to:
#   build_cpu/python/snapllm_bindings.*
```

## Usage

```python
import sys
sys.path.insert(0, "build_cpu/python")
import snapllm_bindings

manager = snapllm_bindings.ModelManager("D:/SnapLLM_Workspace")
```

## Notes

- On Linux/macOS, the SnapLLM static library is built with PIC to support linking into the Python module.
- For GPU builds, replace `build_cpu` with `build_gpu`.
- Diffusion and vision features remain available through the HTTP server; they are not part of the Python bindings in this repo.

## Files

- `snapllm_bindings.cpp` - pybind11 binding implementation
- `CMakeLists.txt` - Build configuration
