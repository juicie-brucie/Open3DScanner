# PLAN #71 — oriented/moving scan, plane-removal toggle A/B — PASS (negative result, per work order)

Follow-up to `2026-10-23-plan71-orient-toggle-not-reached.md` (same-day prior attempt whose
session timed out during setup negotiation before any toggle was sent). This is a fresh
session with the scene/motion plan settled beforehand.

- Date: 2026-10-23
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server (build-tree copy, fixed order 2)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (12:53:07) — not used
- Preflight: pwd OK; no conflicting process; daemon: RPCServer(12187) running; freshness: binary
  12:53:07 >= newest source main.cpp 12:52:55
- Interposer: expect line 2 `[vulkan_disable_interpose] loaded` — confirmed present
- Run start: 2026-10-23 01:55:25

## Scene setup (operator-reported)

Scanner on tripod, ~16" back / ~11" above a stationary turntable, aimed at its center
(angle ~= atan(11/16) ~= 34.5 deg) — tracking did not lock at a further 34"/16" distance tried
in the prior attempt, so the operator stayed closer. Motion: **the scanner itself** was panned
by hand across the flat surface at three heights — 9", 11", 13" — satisfying #71's documented
motion prerequisite (the scanner moving, not the turntable). Turntable itself did not rotate.
**Scattered-object/marker clearing state was never confirmed either way** — unchanged caveat
from the prior attempt; treat the plane as possibly not fully clear of clutter.

## Command

```
./build/tools/scan_server/scan_server --live 8787
```
Toggle commands sent over the WS control channel (minimal hand-rolled WebSocket client, same
shape as `docs/recomp/HW-TEST-QUEUE.md` SS T2's sender):
```json
{"cmd":"set_plane_removal","auto":true}
{"cmd":"set_plane_removal","auto":false}
{"cmd":"set_plane_removal","auto":true}
```

## Result

- Exit code: `0` (`[launcher] Worker exited normally with code 0.`)
- Verdict: **PASS** — a complete, successful A/B/A toggle sequence with a clear result (see
  "Primary finding" below); this is not a blocked or invalid run
- Raw log: `plan71-plane-removal-ab.raw.log` (full session, same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` (line 2)

## Toggle sequence (vendor acceptance + point-count state)

| Stage | Toggle | `obscan_scan_plane_remove` result | Accepted at tick | Points at accept |
| --- | --- | --- | --- | --- |
| Baseline (default off) | — | n/a (no call yet) | tick 900 (pre-toggle) | 1,266,809 |
| ON | `plane_remove(true)` | `OB_SCAN_SUCCESS (0)` | tick 955 | 1,267,446 |
| OFF | `plane_remove(false)` | `OB_SCAN_SUCCESS (0)` | tick 1416 | 1,356,082 |
| ON | `plane_remove(true)` | `OB_SCAN_SUCCESS (0)` | tick 1745 | 1,358,934 |

All three toggle calls were accepted by the vendor backend, both directions, twice.

## Supporting screenshots (operator-attributed, supplied after the session)

Three screenshots from the live session, supplied by the operator after the fact (not
captured in real time during the toggle sequence above), viewed directly by this agent:

- `2026-10-23-plan71-plane-removal-ab-screenshot1-occlusion.jpg` — HUD: `tracking
  FAILURE_MARKERLESS (5)`, `points 19875`, `coverage 88% covered, 542 under-covered pts`,
  `frame #1401`, `data 9.7 fps`, `render 60.0 fps`. This is the accidental-occlusion moment
  during the pan step (operator-confirmed cause, see Checks table above).
- `2026-10-23-plan71-plane-removal-ab-screenshot2-recovered.jpg` — HUD: `tracking SUCCESS (0)`,
  `points 19979`, `coverage 96% covered, 340 under-covered pts`, `frame #3069`, `data 10.0 fps`,
  `render 59.9 fps`. Recovered state, later in the session.
- `2026-10-23-plan71-plane-removal-ab-screenshot3-final.jpg` — HUD: `status disconnected`,
  `points 5531571`, `coverage 96% covered, 470 under-covered pts`, `tracking --`, `frame #-1`.
  Post-teardown final-mesh snapshot — the `points` figure (5,531,571) matches the
  `num_vertices=5531571` already logged from `obscan_scan_get_realtime_model_mesh` in this
  record's Crash sweep section, confirming this is the session's final state.

All three screenshots show a cluttered scene (multiple distinct object shapes visible in the
point cloud, not a single clean flat surface) — visual confirmation of the open
scattered-object/marker-clearing caveat noted elsewhere in this record.

**Limitation, stated plainly:** none of these three screenshots is mapped to the specific
toggle ticks logged above (baseline tick 900, ON tick 955, OFF tick 1416, ON tick 1745, end
tick 2100). The HUD's `frame` counter is a different counter than this record's internal
`tick` count, and no mapping between the two was captured. These screenshots are **general
supporting evidence of the session's visual state** (pre-toggle occlusion, mid-session
recovery, final result) — not frame-exact proof tied to any individual toggle transition. The
primary finding above rests on the operator's live, continuous visual observation through the
toggle sequence, not on these three discrete, retroactively-supplied screenshots.

## Primary finding: the toggle produced no observable geometric effect

**The operator watched the rendered model/point cloud live in the browser through all three
toggle transitions and reports, verbatim: "nothing visually changed whatsoever during those
toggles"** — the plane's points did not vanish when plane-removal was enabled, and did not
reappear differently when it was disabled or re-enabled.

This is corroborated, not contradicted, by the server-side point-count series: growth rate was
high in the window right after the first toggle (~192 pts/tick, tick 955 to 1416) but dropped
sharply and stayed low through both the OFF window (~8.7 pts/tick, tick 1416 to 1745) and the
second ON window (~5.4 pts/tick, tick 1745 to 2100) — a pattern that does **not** track the
toggle state (OFF and the second ON show similarly low growth) and is much more consistent with
the accumulating model cloud **saturating** as repeated passes re-cover already-captured
geometry than with any toggle-driven effect. This matches the exact confound this row's own
history already named (`docs/recomp/PLAN.md` row #71: *"the count-only A/B is confounded by
accumulation"*) — point-count alone was never going to settle this, which is why the operator's
direct visual read is the record's primary evidence here, not the count series.

Per the work order's own stated handling of this case (*"If the plane is not identified even
after the orientation/motion... that is a result: record it — the algorithm/producer limit —
rather than forcing a count A/B"*), **this is recorded as a valid negative result**: under this
scene/motion setup, the vendor's plane-removal toggle was accepted every time but produced no
observable change to the rendered geometry.

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Scanner motion (not turntable) satisfies the documented prerequisite | **[PASS]** operator confirms the scanner itself was panned at 3 heights; turntable stationary |
| 2 | All three toggle calls accepted by the vendor | **[PASS]** `OB_SCAN_SUCCESS (0)` x3, see table above |
| 3 | Operator's direct visual read of the toggle's effect | **[PASS, negative result]** "nothing visually changed whatsoever during those toggles" |
| 4 | Point-count series as a secondary cross-check | **[INCONCLUSIVE]** does not cleanly track toggle state; consistent with accumulation saturation, not a toggle effect — not used to override the visual read |
| 5 | Tracking during the session | **[PASS, with a noted incident]** `SUCCESS=9162 LOST=31 FAILURE_MARKERLESS=106` — the LOST/FAILURE_MARKERLESS stretch occurred during the pan step, **before any toggle was sent**; operator attributes it to an accidental occlusion of the lens during the pan, not a height-transition or toggle artifact. Tracking recovered to SUCCESS on its own before the first toggle. |

## Crash sweep

`scripts/hw-postflight.sh plan71-plane-removal-ab '2026-10-23 01:55:25' scan_server` ->
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-23 01:55:25 — safe to record
'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

Session-end teardown was clean: final `obscan_scan_get_realtime_model_mesh -> OB_SCAN_SUCCESS
(0), num_vertices=5531571, num_triangles=9031057`; `our_plane_removal fits=0 kept=0 cut=0`
(confirms no **client-side** plane removal ran — only the vendor's own toggle was exercised,
as intended); `[launcher] Worker exited normally with code 0.`

## Open questions

- Scattered-object/marker clearing state on the turntable was never confirmed — does a clean
  plane (no clutter) change the result, or is the no-visible-effect finding independent of
  that?
- Is the toggle's lack of visible effect a property of this specific scene/geometry (e.g. the
  plane wasn't large/flat/well-identified enough for the vendor's own plane-fit to engage), or
  does it hold generally? Not something this record can answer — the work order's own language
  treats "not identified" as a valid algorithm/producer-limit result, not something to chase
  further here.
- No board move recommended by this agent — reporting the result for the dispatcher to judge
  against row #71's acceptance criteria, as instructed.
