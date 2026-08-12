"""Static cancellation contract checks for the HTTP inference gate.

This test intentionally inspects the production seams rather than creating a
real model.  It protects the cheap, deterministic invariants that can be
verified without GPU/model fixtures: every gate acquisition must be paired
with an RAII guard, and cached-context streaming must propagate a client
disconnect to the bridge callback.  The latter currently fails and records
the production blocker described in docs/qa/cancellation-timeout-loop-2026-08-11.md.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
SERVER = (ROOT / "src" / "server.cpp").read_text(encoding="utf-8")
CONTEXT = (ROOT / "src" / "context_manager.cpp").read_text(encoding="utf-8")


def main() -> int:
    acquisitions = len(re.findall(r"if \(!acquire_inference_gate\(", SERVER))
    guards = len(re.findall(r"InferenceGateGuard gate_guard\(", SERVER))
    if acquisitions != guards:
        print(f"FAIL gate RAII coverage: {acquisitions} acquisitions, {guards} guards")
        return 1
    print(f"PASS gate RAII coverage: {acquisitions} acquisitions, {guards} guards")

    # ContextManager::TokenCallback is currently void, so the bridge adapter
    # must not unconditionally return true.  Returning true after sink failure
    # causes generation (and the HTTP gate) to continue after disconnect.
    adapter = re.search(
        r"auto bridge_callback\s*=.*?\{(?P<body>.*?)\n\s*\};",
        CONTEXT,
        re.DOTALL,
    )
    if adapter is None:
        print("FAIL cached-context bridge callback not found")
        return 1
    if re.search(r"return\s+true\s*;", adapter.group("body")):
        print("FAIL cached-context disconnect is not propagated: bridge callback always returns true")
        return 1
    print("PASS cached-context disconnect propagation")
    return 0


if __name__ == "__main__":
    sys.exit(main())
