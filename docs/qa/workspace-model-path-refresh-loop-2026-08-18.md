# Workspace/model path and refresh QA (v1.17.39)

- Docker defaults are supplied through `SNAPLLM_WORKSPACE_ROOT=/workspace` and
  `SNAPLLM_MODELS_PATH=/models`; persisted valid paths remain configurable.
- Invalid/stale persisted host paths fall back to the mounted Docker path,
  avoiding a misleading `D:\\Models` value inside Linux containers.
- Browser model discovery now calls the daemon's `/models/scan` endpoint.
  Refresh therefore works in Docker instead of returning an unconditional
  empty list because the browser cannot read the host filesystem.
- Settings explains the restart requirement and Docker host/container path
  distinction; browser restart instructions include a copyable command.

Evidence:

```
npm --prefix desktop-app run test -- --run  3 files, 17 tests PASS
npm --prefix desktop-app run lint           PASS
cmake --build build --config Debug --target snapllm_cli --parallel 2 PASS
node scripts/check_versions.mjs             version_consistency: 1.17.39
```
