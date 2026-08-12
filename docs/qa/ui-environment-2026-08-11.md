# Desktop UI environment QA — 2026-08-11

## Clean environment

The desktop app was copied to an isolated temporary directory and installed
from the committed lockfile:

```text
C:\Users\mahes_h9w44qg\AppData\Local\Temp\snapllm-ui-qa-bf6a65444c31468c9d80d7605ee9580c
```

Command: `npm ci --no-audit --no-fund`

Result: 522 packages installed. `npm ls --depth=0` resolved all declared
dependencies without missing or invalid packages.

## Checks

| Check | Result |
| --- | --- |
| `npm run lint` | PASS |
| `npx tsc --noEmit` | PASS |
| `npm test -- --run` | PASS — 2 files, 12 tests |
| `npm run build` | PASS — 3,230 modules transformed |
| `npm run audit` | PASS — npm audit reported no advisories |
| `npx playwright test --list` | PASS — 9 tests discovered |
| Playwright UI subset | 4 passed, 1 blocked by daemon |

The production build emits only non-blocking warnings about stale browser data
and a JavaScript chunk larger than 500 kB.

## E2E blocker

The route-rendering test fails when the SnapLLM daemon is not running. The UI
correctly renders its offline state (`SnapLLM daemon is offline`), while the
Vite proxy logs `ECONNREFUSED 127.0.0.1:6930`. Start the API daemon on port
6930 before running the API-dependent Playwright tests.

The following browser checks passed without the daemon:

- sidebar and command-palette navigation;
- accessible names for icon-only controls;
- responsive overflow checks on desktop and mobile.
