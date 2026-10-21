# Correction addendum — 2026-10-21-plan93-hud-half.md

Date: 2026-10-21
Corrects: `2026-10-21-plan93-hud-half.md`

Three defects found by reading the actual session log
(`tests/results/2026-10-21-plan151-93-153-session.raw.log`):

## Defect 1 — Wrong raw log path (citation defect)

Line 33 of the original:
> `from /tmp/scan151_raw.log`

`/tmp/scan151_raw.log` is a temporary file not archived in the results.
The correct archived log is:
> `tests/results/2026-10-21-plan151-93-153-session.raw.log`

## Defect 2 — LOST range end line wrong (clock/line defect)

Line 38 of the original:
> `| LOST | 26 frames | lines 18284–18308 (vendor's own debounce window) |`

Actual last LOST (`tracking_status=1`) line in the session log: **18335**
(confirmed by `grep -n "tracking_status=1 (LOST)" ... | tail -3` → 18331, 18333, 18335).

Corrected row:
> `| LOST | 26 frames | lines 18284–18335 (vendor's own debounce window) |`

## Defect 3 — FAILURE_MARKERLESS range start line wrong (clock/line defect)

Line 39 of the original:
> `| FAILURE_MARKERLESS | 475 frames | lines 19287–19295 (vendor's settled markerless state) |`

475 frames cannot fit in lines 19287–19295 (9 lines). Actual first
`tracking_status=5 (FAILURE_MARKERLESS)` line in the session log: **18337**
(confirmed by `grep -n "tracking_status=5" ... | awk ... → first:18337 last:19295`).

Corrected row:
> `| FAILURE_MARKERLESS | 475 frames | lines 18337–19295 (vendor's settled markerless state) |`

The end line (19295) and the SUCCESS recovery line (19297) in the original are correct.

## Verdict and board

No change to verdict (PASS) or board move — all five checks remain valid.
The server-side counts (26 LOST, 475 FAILURE_MARKERLESS) are confirmed correct
by the session log's histogram line 32465:
`tracking_status samples=8814 histogram: SUCCESS=8313 LOST=26 FAILURE_MARKERLESS=475`
