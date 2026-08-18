# API-key and model-path UI QA (v1.17.39)

- Server Settings now has a visible **Generate Key** action. It calls the
  loopback bootstrap route and applies the key in memory.
- Model Hub now displays the server-reported models directory and has a
  direct **Scan Path** action, so `/models` (Docker) or the configured native
  path is recognizable before loading a model.

Evidence:

```
npm --prefix desktop-app run lint           PASS
npm --prefix desktop-app run test -- --run  3 files, 17 tests PASS
npm --prefix desktop-app run build          PASS
node scripts/check_versions.mjs             version_consistency: 1.17.39
```
