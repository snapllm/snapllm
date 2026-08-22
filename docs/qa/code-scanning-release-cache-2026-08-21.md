# Code-scanning release workflow review — 2026-08-21

## Finding inventory

The `maheshvaikri-code/snapllm` Code Scanning API reported 532 alerts in
total. Six were open, all `actions/cache-poisoning/poisonable-step` findings
in `.github/workflows/release.yml` (lines 100, 102, 106, 108, 267, and 376
in the scanned revision). The flagged pattern was a privileged checkout of
`needs.validate-release.outputs.commit_sha` in a manually dispatched
workflow, combined with dependency caching in the UI build.

## Remediation

- Release automation now runs only on a maintainer-created `vMAJOR.MINOR.PATCH`
  tag push.
- Every job checks out the immutable event commit (`github.sha`); no job
  consumes a commit SHA produced by a workflow input or untrusted job output.
- The release UI job no longer enables npm dependency caching.
- Release creation targets the same immutable event commit.

This preserves the existing tag/version validation while removing the tainted
manual-input checkout and cache trust boundary.

## Local validation

```text
version_consistency: 1.17.41
git diff --check: passed (line-ending warnings only)
```

The GitHub alert list cannot reflect this fix until commit `v1.17.41` is
pushed and the workflow/CodeQL scan runs on the new repository. No alert was
dismissed as part of this remediation.
