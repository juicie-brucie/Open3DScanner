# PLAN #71 — attempt 9, single-session A/B (floor plane in scene) — no step-change detected at the toggle boundary

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (synced to origin/main @ `2174a66`)
- Target: build/tools/scan_server/scan_server (build-tree copy, fixed order 2: build/tools/<target>/<target>)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (22:17:05) — not used
- Preflight: `hw-preflight: OK`; pwd OK; no conflicting process; daemon RPCServer(26387,) running; freshness binary 22:17:05 >= newest source `fast_encode.h` 22:06:30
- Interposer: `[vulkan_disable_interpose] loaded` — present, line 2
- Run start: 2026-10-20 23:14:48
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`; fresh after power-cycle; scan_server first process to access device this boot; floor plane in scene, markers visible, scanner static throughout

## Command

```
OPENSCANNER_PLANE_REMOVE=1 ./build/tools/scan_server/scan_server --live 8787
# toggle OFF after ~60s via WebSocket:
# {"cmd":"set_plane_removal","auto":false}
```

## Result

- Exit code: SIGKILL on worker (`obscan_scan_pause -> OB_SCAN_SUCCESS` logged; normal paced teardown; force-killed as before)
- Verdict: **the vendor accepts the toggle; no step-change in points-per-tick is detectable at the toggle boundary**
- Raw log: `2026-10-20-plan71-attempt9-ab.raw.log` (same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` — present, line 2

## Toggle log

```
[worker] plane_removal: obscan_scan_plane_remove(true) -> OB_SCAN_SUCCESS (0) accepted [auto 0->1 startup]
[worker] plane_removal: obscan_scan_plane_remove(false) -> OB_SCAN_SUCCESS (0) accepted [auto 1->0 tick=746 points=862897]
```

## A/B points-per-tick comparison

Phase A and B windows measured from `[perf] window ... points=` (mean over 5 s / ~49 ticks):

| Phase | Condition | n windows | min | max | mean |
|---|---|---|---|---|---|
| A (settled) | `plane_remove=true` (ON) | last 6 of 15 | 19,683 | 19,807 | 19,734 |
| B | `plane_remove=false` (OFF) | first 6 of 17 | 19,721 | 19,899 | 19,810 |
| B (full) | `plane_remove=false` (OFF) | 17 | 19,721 | 19,958 | 19,832 |

Phase A settled range: 19,683–19,807 (6 windows, mean 19,734).
Phase B range: 19,721–19,958 (17 windows, mean 19,832).
Overlap: yes — the Phase B range overlaps completely with the Phase A settled range.
Step-change at toggle: not detectable. The first Phase B window (19,736) is within Phase A's settled band.

## Session-end summary (from binary)

```
[perf] summary ticks=1609 mean_points=18973.7 ...
[worker] tracking_status samples: (inferred) 4498/4498 SUCCESS
```

## Checks

| # | Check | Result |
|---|---|---|
| 1 | Pipeline healthy post-power-cycle; scan_start reached | **[PASS]** `frame_ready_count=13`, `obscan_scan_start -> OB_SCAN_SUCCESS` at ~6 s |
| 2 | Startup plane removal applied | **[PASS]** `obscan_scan_plane_remove(true) -> OB_SCAN_SUCCESS accepted [auto 0->1 startup]` |
| 3 | Toggle OFF accepted mid-session | **[PASS]** `obscan_scan_plane_remove(false) -> OB_SCAN_SUCCESS accepted [auto 1->0 tick=746 points=862897]` |
| 4 | Static scene throughout (no tracking LOST) | **[PASS]** 4498/4498 `tracking_status=0 (SUCCESS)` |
| 5 | Step-change in points-per-tick at toggle boundary | **[FAIL — not observed]** Phase A settled mean 19,734; Phase B mean 19,832; delta ~98 points/tick (0.5%); within the natural tick-to-tick variance (~±200 points); no step visible at the toggle |

## Interpretation

The vendor accepted both calls (`OB_SCAN_SUCCESS` each direction). The `points=` metric did not show a detectable step-change at the toggle. Two candidate explanations, not diagnosed:

1. The auto plane-removal toggle (`obscan_scan_plane_remove`) acts on a detected ground plane, which the vendor's algorithm may not have identified in this scene geometry — if no plane is detected, the toggle has nothing to remove and the point count is unchanged.
2. The `points=` metric in the `[perf]` line is the cumulative cloud's published point count per tick, not the per-frame point delta. A plane-removal effect that removes points already in the cloud may manifest differently (a drop in absolute published count, or a reduced increment rate as new frames stop adding floor points) — the per-tick window mean may smooth over a real but smaller effect.

## Open questions

- Did the vendor's algorithm actually detect a plane in this scene? There is no log line confirming plane detection; the vendor's auto-detection result is opaque to the log.
- Would `OPENSCANNER_TURNTABLE_PLANE_REMOVE=1` (the turntable-specific variant, aimed at a horizontal plane under a turntable rather than a general ground plane) produce a different result?
- Is the right metric the per-tick `points=` mean, or the absolute point count after N ticks (which would show a plateau if floor-plane addition is suppressed)? The absolute cumulative count at toggle was `points=862,897` — a post-session comparison would need two full sessions.

## Crash sweep

`scripts/hw-postflight.sh plan71-attempt9-ab '2026-10-20 23:14:48' scan_server` →
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-20 23:14:48 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

## Board

- Row move: #71 stays `HW` — the toggle mechanism is confirmed working end-to-end (both directions, `OB_SCAN_SUCCESS`, correct log lines, static scene, 4498/4498 SUCCESS tracking). The row's owed observable (a measurable per-tick point-count effect from the plane-removal toggle) was not observed in two separate A/B attempts (attempts 7 and 9), each with the vendor accepting the calls. Whether the effect is absent because no plane was detected, because the metric is wrong, or because the vendor requires a specific scene setup (turntable plane vs general ground plane) is not determinable from these runs.
