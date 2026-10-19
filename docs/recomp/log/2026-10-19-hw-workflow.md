- 2026-10-19 — the **hw-test workflow** gains its mechanical gates. `scripts/hw-preflight.sh`: worktree + branch; a
  **deterministic binary selection** (explicit path, then `build/tools/<target>/<target>`, then the `.app` bundle copy —
  reporting the other copy's mtime, #107); freshness against the **newest** source in the target's directory (headers
  included, so #89/#111-style header changes count); name-anchored `pgrep -x`; the interposer expectation.
  `scripts/hw-postflight.sh`: a crash sweep matched on the report's own `app_name` + `timestamp`, archiving the
  **earliest** in-window report, listing any others, printing its sha256, and stating explicitly when none matched.
  `tests/results/_TEMPLATE.md` is the record skeleton — its `_`-support-file convention landed separately in #290, which
  had to precede this PR because CI runs the gate from `origin/main`. Verified locally: a synthetic sweep ignored a
  pre-start report and another app's crash and archived the earliest in-window one; preflight chose the build-tree copy
  and named the bundle copy.
