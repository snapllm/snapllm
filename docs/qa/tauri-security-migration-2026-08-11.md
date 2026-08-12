# Tauri security migration — 2026-08-11

## Completed

- Migrated the desktop crate from Tauri 1.8 to Tauri 2.11.5.
- Migrated `tauri-build` to 2.6.3.
- Migrated the configuration to the Tauri 2 schema (`devUrl`,
  `frontendDist`, `app.windows`, and `app.security`).
- Updated the resource-directory lookup to the Tauri 2 `Manager::path()` API.
- Replaced the Tauri 1 dialog/filesystem JavaScript APIs with Tauri 2
  `plugin-dialog` and `plugin-fs` APIs and registered both Rust plugins.
- Added a default capability with explicit dialog access and scoped model/
  workspace filesystem permissions; the old unrestricted Tauri 1 allowlist is
  no longer present.
- Regenerated `Cargo.lock`.

Evidence:

```text
cargo check --manifest-path desktop-app/src-tauri/Cargo.toml
Finished `dev` profile [unoptimized + debuginfo]
cargo test --manifest-path desktop-app/src-tauri/Cargo.toml
test result: ok. 0 passed; 0 failed
npm run lint
exit 0
npx tsc --noEmit
exit 0
npm test -- --run
Test Files 2 passed; Tests 12 passed
npm run build
âœ“ built in 1.38s
npm run tauri build -- --debug --no-bundle
Built application at: desktop-app/src-tauri/target/debug/snapllm-desktop.exe
npm run audit
npm audit: passed (no advisories)
npx tauri info
Tauri 2.11.5; API 2.11.1; fs/dialog plugins 2.x; WebView2 detected
```

The release workflow now installs `libsoup-3.0-dev` alongside WebKitGTK 4.1,
matching the Tauri 2 Linux dependency graph. The previous libsoup2-only setup
could fail native dependency detection before compilation.

The Vite build now splits React, charts, markdown, syntax highlighting, and
other vendor code into cacheable chunks. This removes the previous single
2.1 MB application chunk; the syntax-highlighting chunk remains intentionally
large because that third-party language grammar bundle is loaded only by the
markdown renderer.

Syntax highlighting is lazy-loaded behind a Suspense fallback, so the grammar
bundle is not fetched for ordinary prose responses.

Filesystem capabilities also include `$HOME/Models/**`, so model discovery is
not limited to Windows drive-letter paths when the same desktop bundle runs on
Linux or macOS.

## Remaining upstream advisory

`cargo audit` no longer reports the old glib 0.15/rand 0.7 chain, but reports
17 allowed upstream warnings. The current Tauri/WebKitGTK stack still resolves
`glib 0.18.5`, which RustSec marks with RUSTSEC-2024-0429; GTK3 and several
Unicode/proc-macro crates are also marked unmaintained upstream.
This is an upstream WebKitGTK/Tauri platform dependency; it cannot be fixed by
an application-level Cargo override without breaking ABI-compatible GTK types.
The desktop build now uses the maintained Tauri 2 generation, but a complete
removal requires a future platform backend that no longer depends on GTK3.

The dependency path is confirmed by `cargo tree --target all -i glib@0.18.5`:
`snapllm-desktop -> tauri 2.11.5 -> wry 0.55.1 -> webkit2gtk 2.0.2 -> gtk /
glib`. CI runs `cargo audit` without an ignore list; these advisories remain
visible as warnings and are not silently dismissed.
