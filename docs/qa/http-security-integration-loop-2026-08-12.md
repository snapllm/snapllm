# HTTP security integration loop — 2026-08-12

The rebuilt CLI passed the full PowerShell security integration harness:

```text
server_security_integration: all checks passed
```

The harness now accepts the API's two valid fail-closed validation statuses:
400 for legacy malformed-input paths and 422 for structured route/parameter
validation. Unsupported Messages image blocks similarly accept 422 or the
legacy 501 response. Authentication, origin, host-header, path, public-bind,
and network-guard checks remain strict.
