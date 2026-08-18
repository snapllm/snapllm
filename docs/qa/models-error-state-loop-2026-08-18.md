# Models page error-state QA (v1.17.38)

The Models page now preserves the real API failure reason. A daemon returning
401 is shown as “API key required to load models” with guidance to apply the
key in Server Settings; other failures retain their API error text. A Retry
button reissues the models query without requiring a page reload.

Evidence:

```
npm --prefix desktop-app run lint           PASS
npm --prefix desktop-app run test -- --run  3 files, 17 tests PASS
npm --prefix desktop-app run build          PASS
node scripts/check_versions.mjs             version_consistency: 1.17.38
```
