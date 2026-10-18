- 2026-10-17 — dispatcher: verification rebuilt — `scripts/verify-pr.py`
  (build, records-in-place, plan syntax, archival hashes, hook invariants) runs
  as the CI `verify-pr` status from a trusted copy on `main`; `verdict-gate`
  sets `verdict` automatically for script-tier PRs and from a verifier's
  anchored `VERDICT: ... @ <sha>` comment for agent-tier PRs; `main` gets
  branch protection + auto-merge. Agent tier: code, `.github/`, `.claude/`,
  `docs/recon/`, and `tests/results/**` addenda.
