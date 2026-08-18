# Settings path recognition QA (v1.17.38)

Docker's `SNAPLLM_MODELS_PATH=/models` now overrides a stale native path from
the shared persisted config. Server Settings explains the distinction between
the container-visible path and the host mapping, and labels the live path
reported by `/api/v1/config`.

Evidence:

```
node scripts/check_versions.mjs             version_consistency: 1.17.38
npm --prefix desktop-app run lint           PASS
npm --prefix desktop-app run test -- --run  3 files, 17 tests PASS
cmake --build build --config Debug --target snapllm_cli --parallel 2 PASS
```
