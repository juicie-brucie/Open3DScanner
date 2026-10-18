# Operator note — `2026-10-18-scan-server-scale-acceptance.md`

**Date:** 2026-10-18
**Corrects/supplements:** [`2026-10-18-scan-server-scale-acceptance.md`](./2026-10-18-scan-server-scale-acceptance.md)
**Raised by:** the user, after the run.

This file is additive, per `tests/hardware/README.md:10` — the run record is kept
unedited.

## What happened that the record doesn't capture

The run was **not** fully unattended. The user picked up and manually moved the
physical scanner partway through the session, after observing (via the live UI)
that the point count was not going to reach 1,000,000 on its own. Neither the
`hw-test` agent (no visibility into physical operator action) nor the main
session's UI-watch log explicitly recorded this as an event — it only shows up
indirectly, as a sudden acceleration in the per-tick series.

## Where it shows up in the record

The published per-tick series
([`2026-10-18-scan-server-scale-acceptance.md`](./2026-10-18-scan-server-scale-acceptance.md),
"Per-tick log lines") sits essentially flat through tick 600 (340 points), then
jumps sharply:

```
tick 600/1200: color_cloud num_points=340
tick 650/1200: color_cloud num_points=6166
tick 700/1200: color_cloud num_points=13494
...
tick 1150/1200: color_cloud num_points=306327
```

That inflection (tick ~600–650, roughly 60–65s into the 120s session) is when the
manual movement happened. The main session's own parallel UI observation shows
the same thing from the client side: points stayed in the low hundreds through
several samples, then jumped from 342 to 17,600 between two ~10s-apart readings.

## Why this matters for row #32's conclusion

The original record attributed the shortfall against the ≥1,000,000-point bar to
"this session's scan covering less geometry… a function of how the device was
moved" — correct as stated, but incomplete: it reads as if the session was
passively monitored throughout, when in fact the operator actively intervened to
try to close that gap and it still wasn't enough. That is a *stronger* result in
one sense (the burst shows the fix handles a sudden, large rate of new geometry
without stalling — render stayed at 60fps straight through the inflection) and
a more specific gap in another (even with deliberate mid-session movement, one
~120s session covering a modest area did not accumulate past ~306K live /
800K final — reaching ≥1M will need either sustained movement over more of the
session, a longer session, or a larger/richer scene).

## No change to the verdict

The run record's verdict (`FAIL` on the literal ≥1M bar, server otherwise
behaving correctly) stands. This note only supplies the missing cause of the
tick 600–650 inflection and sharpens what a follow-up rerun should do
differently: move the scanner across new geometry for most/all of the session,
not partway through it.
