# Release packaging loop — 2026-08-12

The standalone Windows and Linux packagers previously defaulted to CUDA
(`build_gpu` and `*-cuda` names), while the GitHub release workflow builds and
publishes CPU archives from `build_cpu`. Both scripts now accept an optional
`cpu|gpu` mode and default to `cpu`, matching the workflow; `cuda` remains an
accepted backward-compatible alias for `gpu`.

Validation:

```text
bash -n package_release.sh
exit 0

node scripts/check_versions.mjs
version_consistency: 1.17.8
```

The scripts still require their platform-specific build output and are not
invoked against a missing build directory during this local loop. They now
also require `desktop-app/dist/index.html`, copy the full UI into `ui/`, and
launch the server with `--ui-dir ui`, matching the GitHub release packages.
The Windows model launcher now delegates validation to a packaged PowerShell
script instead of embedding metacharacters inside a parenthesized batch block.

End-to-end Windows dry runs now pass for both modes:

```text
package_release.bat 1.17.8 cpu  -> exit 0, 3,954,734 bytes
package_release.bat 1.17.8 gpu  -> exit 0, 455,014,261 bytes
```

Both archives contain `VERSION=1.17.8`, `bin/snapllm.exe`, `ui/index.html`,
the server launchers, and `run_server_with_model.ps1`. The CPU archive contains
zero CUDA DLLs; the GPU archive contains all three CUDA DLLs.
