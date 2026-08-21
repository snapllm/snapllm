# Automatic API-key bootstrap QA (v1.17.40)

SnapLLM now generates a cryptographically strong 32-byte hexadecimal runtime
key when a non-loopback server starts without `SNAPLLM_API_KEY`. The key is
never persisted or returned by the normal configuration endpoint.

For the explicitly supported Docker `loopback-port-publish` deployment, the
UI can call `/api/v1/auth/bootstrap`; the API client retries the original 401
request with the returned in-memory key. Other network guards return 403 from
the bootstrap route and require an operator-provided stable key.

Docker stores the generated key at `/app/.config/snapllm/api_key`, backed by
the repository's writable `./config` volume. Restarts therefore reuse the key
instead of silently generating a different credential.

Evidence:

```
cmake --build build --config Debug --target snapllm_cli --parallel 2  PASS
npm --prefix desktop-app run lint                              PASS
npm --prefix desktop-app run test -- --run                    3 files, 17 tests PASS
npm --prefix desktop-app run build                             PASS
node scripts/check_versions.mjs                                version_consistency: 1.17.40
```
