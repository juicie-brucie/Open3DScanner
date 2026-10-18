- 2026-10-17 — housekeeping (`build`): PR #34 split — hardware run records and the
  Claude Code guardrails moved off the build PR onto `hw/plan8-plan10` and
  `chore/claude-hw-guardrails`; #34 is build-only again. Run artefacts (crash report,
  vendor `scan_log`, SDK log window) archived under `tests/results/` instead of being
  left in `/tmp` and `~/Library/Logs/DiagnosticReports`.
