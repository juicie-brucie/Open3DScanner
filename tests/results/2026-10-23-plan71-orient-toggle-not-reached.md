# PLAN #71 — oriented/moving scan, ORIENT step — toggle step NOT REACHED — BLOCKED

- Date: 2026-10-23
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server (build-tree copy, fixed order 2)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (12:53:07) — not used
- Preflight: pwd OK; no conflicting process; daemon: RPCServer(12187) running; freshness: binary
  12:53:07 >= newest source main.cpp 12:52:55
- Interposer: expect line 2 `[vulkan_disable_interpose] loaded` — confirmed present
- Run start: 2026-10-23 01:46:36

## Scene setup (operator-reported)

Scanner on tripod, ~16" back / ~11" above the turntable's surface, aimed at its center
(angle ~= atan(11/16) ~= 34.5 deg) — closer than originally requested; operator reports
tracking did not lock at the further 34"/16" distance tried first. Turntable itself stationary
throughout; the **scanner** was panned by hand ("a couple pans at a few different heights")
across the flat surface, per #71's documented motion prerequisite
(`docs/recomp/PLAN.md` row 71, `docs/recomp/HW-TEST-QUEUE.md` SS T8-C). Scattered-object/marker
clearing state: **not confirmed** — operator did not re-answer this question.

## Command

```
./build/tools/scan_server/scan_server --live 8787
```
No WebSocket control command (`set_plane_removal`) was ever sent — see Result.

## Result

- Exit code: `0` (`[launcher] Worker exited normally with code 0.`)
- Verdict: **BLOCKED** (the toggle step, which is #71's acceptance criterion, was not reached)
- Raw log: `plan71-orient-toggle-not-reached.raw.log` (full session, same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` (line 2)

**The session reached its own bounded length (3000 ticks, "up to 300s" per the worker banner)
and ran its automatic pause/stop/destroy teardown before any `set_plane_removal` WebSocket
command was sent.** The setup negotiation (distance/angle/motion-vs-turntable back-and-forth)
consumed the live session's time budget; by the time motion was confirmed and reported, the
session had already auto-paused (`obscan_scan_pause -> OB_SCAN_SUCCESS (0)`) and torn down
cleanly on its own, with no operator or agent action.

## What the ORIENT (pan) step itself produced, for the record

| # | Observation | Result |
| --- | --- | --- |
| 1 | Tracking during the scanner pans | **[PASS]** `tracking_status samples=9280 histogram: SUCCESS=9280` — no `LOST`/`FAILURE_MARKERLESS` anywhere in the whole session; clean tracking throughout both the static hold and the pans |
| 2 | Geometry accumulated | Final per-tick model cloud reached `num_points=1,353,030` (max seen `1,353,197`) by tick 2950/3000; final `obscan_scan_get_realtime_model_mesh -> OB_SCAN_SUCCESS (0), num_vertices=3663035, num_triangles=6227569` |
| 3 | Plane-removal toggle | **NOT SENT** — `our_plane_removal fits=0 kept=0 cut=0` (no toggle, vendor or ours, was ever applied this session) |
| 4 | Browser HUD `coverage %` | **Not captured** — coverage is computed client-side only (`ui/coverage.js`/`ui/app.js`), not logged server-side; no screenshot was taken for this step and the operator's report was a general description ("still pretty limited"), not a HUD read |

## Crash sweep

`scripts/hw-postflight.sh plan71-orient-toggle-not-reached '2026-10-23 01:46:36' scan_server` ->
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-23 01:46:36 — safe to record
'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

## Open questions

- Is the live session's bounded length (3000 ticks / ~300s) long enough to accommodate
  real-world scene setup plus the ORIENT pan plus the TOGGLE A/B/A sequence in one session, or
  does #71 as currently scoped need either a longer bound or the toggle sent earlier/concurrent
  with the pan rather than as a distinct step afterward? Not something this record resolves.
- Scattered-object/marker clearing state on the turntable was never confirmed either way.
- Tracking required moving to ~16"/11" after failing to lock at ~34"/16" — is that distance a
  property of this specific scene/lighting, or a more general working-distance constraint for
  this scanner? Not investigated further (observation only).

Lead: n/a (session time budget / setup-negotiation overhead, not a code defect) — a re-run
should have the scene and motion plan fully settled **before** `scan_server --live` is started,
so the toggle sequence fits inside the ~300s bound.
