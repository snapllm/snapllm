# Release Review — SnapLLM v1.3.1 Remediation

**Date:** 2026-07-23
**Scope:** complete public-repository security, correctness, UI, build, and
release-wiring remediation.

The release candidate is reviewed against the Class L brief in
`docs/briefs/snapllm-public-repository-remediation.md`.

Closed review themes include:

- loopback-safe server defaults, remote authentication, exact Host/Origin
  policy, raw security-header preservation, encoded-alias rejection, and
  explicit container network guards;
- canonical path confinement, bounded request and response resources, strict
  image decoding, transactional configuration/context/workspace persistence,
  immutable workspace metadata generations, fail-closed model sizing, and
  exception-safe cross-thread inference lifecycle ownership;
- truthful unsupported-capability behavior across HTTP, C++, and Python;
- same-origin browser wiring, credential-safe renderer errors, bounded offline
  transitions, blob URL ownership, and desktop/mobile UI behavior;
- commit-pinned CI/release actions, verified release commit checkout, consistent
  v1.3.1 identity, dependency audits, and hardened packaging scripts;
- React Router 7.18.1 migration after the v6 security advisories were identified
  by npm audit;
- native, frontend, security, five-state browser, and real TinyLlama Chat
  regressions.

The final code-review vetoes were resolved by making raw bridge pointers
internal, replacing transferable shared-mutex ownership with reference-tracked
context lifetimes, failing model loads when file size cannot be established,
and atomically switching metadata/tensor generations through the workspace
index.

Final independent G4/G5/QA verdicts are recorded in the engagement handoff.
No publication, tag, push, or release was performed.
