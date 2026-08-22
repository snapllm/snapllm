# Server Settings and model-path QA (v1.17.41)

## Scope

Verify that a model-path edit is retained while the daemon is unavailable,
applied after reconnect, and reflected by the Models page.

## Changes verified

- Settings validates the form before either a live save or an offline draft.
- Offline Save Changes now reports a queued local draft instead of a false
  save failure. The draft is applied automatically after `/api/v1/config`
  becomes healthy.
- Live configuration polling runs every five seconds in Settings and Models.
- The selected `default_models_path` is preserved in the nested update payload.
- Restart-required changes expose the Desktop restart control; browser/Docker
  shows the host restart command because a browser cannot restart a process.

## Evidence

```
npm --prefix desktop-app run lint                 PASS
npm --prefix desktop-app run test -- --run       3 files, 17 tests PASS
npm --prefix desktop-app run build                PASS
node scripts/check_versions.mjs                   version_consistency: 1.17.41
```

The test `settings persistence contract` asserts that a user-selected model
path (`D:/Models`) is retained in the server update payload. Live Docker
validation still requires a daemon API key when the container is bound to
`0.0.0.0`; the UI must have that key applied before saving.
