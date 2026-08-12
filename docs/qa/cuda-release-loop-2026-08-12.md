# CUDA Release loop — 2026-08-12

## Build and regression evidence

Host detection found CUDA 12.6.20 and an NVIDIA RTX 4060 Laptop GPU with
8188 MiB. A fresh CUDA Release configure completed successfully.

The first parallel MSBuild attempt hit Windows compiler-output file locks
(`C1083`/`Permission denied`) while generating object files. Rebuilding the
same generated project serially (`/m:1`) completed successfully and produced
the CUDA CLI, CUDA runtime DLLs, benchmarks, and test executables.

```text
cmake --build build_qa_cuda_release_loop_20260812 --config Release --parallel 1
exit 0

ctest --test-dir build_qa_cuda_release_loop_20260812 -C Release --output-on-failure
100% tests passed, 0 tests failed out of 8
Total Test time = 3.97 sec
```

These tests validate the CUDA-enabled build and portable lifecycle/security
contracts. They do not inject a CUDA device failure, force multi-GPU
failover, or prove runtime VRAM rebalancing under pressure. Those remain an
explicit hardware-test requirement.
