# Correction — `2026-10-18-oq7b-listenerless-smoke-test-rerun.md`

**Dated**: 2026-10-18 · **Author**: dispatcher, from the verifier's finding on PR #207 ·
**Corrects**: the record named above (never edited in place).

**The figure**: the record's probe table and its checks table give the vendor's detection event
as "~991ms" after context-create. That is the *millisecond component of the wall-clock
timestamp* (`20:26:46.991893`), not an elapsed time. The correct figures, from the companion log:

- context-create batch: `[10/18 20:26:45.960483]` (its last init line ~45.968)
- watcher detection: `[10/18 20:26:46.991893]` → **~1031 ms** after context-create
- internal device list updated: `[10/18 20:26:47.995202]` → **~2035 ms** (the record's figure
  here is exact: 47.995202 − 45.960483 = 2034.7)

**Nothing in the conclusion moves**: detection at 46.99 still falls between samples 3 (46.968)
and 4 (47.468), and samples 6/7 and 7/7 remain ~473 ms and ~973 ms *after* the list update. The
"settled for the window observed" verdict and the residual are unaffected.
