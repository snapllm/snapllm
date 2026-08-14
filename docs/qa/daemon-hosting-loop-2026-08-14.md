# Daemon hosting reliability loop — 2026-08-14

The Desktop Tauri shell now supervises the loopback daemon after startup. It
polls `127.0.0.1:6930` every two seconds, waits for three failed probes before
restarting, and caps automatic restarts at five. An explicit Stop disables the
supervisor, so user shutdown is not undone.

The Windows login-task installer now sets a working directory and configures
five restart attempts with a one-minute interval and no execution time limit.
Linux systemd and macOS launchd already had restart/KeepAlive policies.

Validation:

- `cargo fmt --manifest-path desktop-app/src-tauri/Cargo.toml -- --check` — passed
- `cargo check --manifest-path desktop-app/src-tauri/Cargo.toml` — passed
- PowerShell script parse via `[scriptblock]::Create(...)` — passed
- `git diff --check` — passed

The browser-only Vite UI cannot spawn or supervise a local process by design;
it polls health every five seconds and clearly instructs the user to keep the
daemon running or use SnapLLM Desktop.
