# Core Release Regression Loop — 2026-08-11

## Scope

Fresh Windows Visual Studio Release configuration using CPU-only options. Existing build directories were preserved; this loop used `build_qa_core_release_20260811`.

## Configuration

```text
cmake -S . -B build_qa_core_release_20260811 -DCMAKE_BUILD_TYPE=Release -DSNAPLLM_CUDA=OFF -DSNAPLLM_ENABLE_DIFFUSION=OFF -DSNAPLLM_ENABLE_MULTIMODAL=OFF -DSNAPLLM_ENABLE_PYTHON_BINDINGS=OFF -DSNAPLLM_OPENMP=OFF
```

Configuration completed successfully with Visual Studio 17 2022 and SnapLLM v1.17.8. CMake emitted only a developer-policy warning (`CMP194`) from the external ggml assembler detection.

## Build

```text
cmake --build build_qa_core_release_20260811 --config Release --parallel 4
```

Result: exit code 0. All core libraries, CLI, and test executables were produced. The compiler emitted existing upstream/deprecation warnings (MSVC conversion warnings in llama.cpp, `getenv` deprecation in workspace_paths.h, and `llama_n_vocab` deprecation); no compilation errors occurred.

## Regression suite

```text
ctest --test-dir build_qa_core_release_20260811 -C Release --output-on-failure

100% tests passed, 0 tests failed out of 8
Total Test time (real) =   2.21 sec
```

Passed tests:

- server_security
- context_persistence
- server_limits
- context_lifecycle
- model_lifecycle
- workspace_metadata_security
- request_router
- prefetch_engine

## Focused checks

`snapllm_benchmark_throughput.exe --help` did not return within the short verification window and was terminated. No benchmark result is claimed. The request-router and prefetch-engine tests are included in the green CTest run above.

## Decision

Core CPU Release regression gate: **PASS** (8/8 tests). Throughput benchmark evidence remains pending a controlled model-backed run and is intentionally not represented as passing here.

<!-- [default 1/10] hypothesis: fresh CPU Release configuration catches regressions independent of prior build artifacts | result: configure/build/ctest pass; throughput help did not complete | next: done for core regression, separate benchmark loop required -->
