# PLAN #153 — exposure on-change: picture-unchanged check — PASS

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server  (build-tree copy; binary 12:53:07)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (12:53:07) — refreshed only on an app build (#107)
- Preflight: continued from session started 2026-10-21 14:23:19 (preflight OK; interposer confirmed)
- Run start: 2026-10-21 14:23:19
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`; fresh after power-cycle; scan_server first access

## Context

Row #153 landed the exposure-on-change fix (PR #371): the
`obscan_scan_config_combined_exposure_and_laser_current` call is now applied
once (on change or before scan_start) rather than every tick. The prior
hardware record (`2026-10-20-plan153-live-run.md`) confirmed `exposure_applies_total=0`
and `exposure n=0` per window over 1344 ticks, but had no browser client connected
(`clients=0.0`) — the picture-unchanged check was deferred. This record covers
that owed observation.

## Command

```
./build/tools/scan_server/scan_server --live 8787
# browser: http://localhost:8787 (WebGPU backend, coordinator observing HUD)
```

Session ran ~5 min; covered both the #151 browser render-loop observation and the
#93 markerless HUD observation in the same session. See raw log
`2026-10-21-plan151-93-153-session.raw.log`.

## Browser HUD state during observation (coordinator-relayed)

| Metric | Value |
|---|---|
| backend | webgpu |
| status | connected |
| scan | scanning (live) |
| frame age p50 | 6.3 ms |
| frame age p95 | 13.9 ms |
| data fps (client) | 10.0 fps |
| render fps (client) | 60.0 fps |
| points | 10,595 (accumulating normally) |
| coverage | 80% covered, 2,462 under-covered pts |
| tracking | SUCCESS (0) |

Note: 3D canvas was dark from the coordinator's orbit camera angle (geometry not
in the default view direction); direct point-cloud visual inspection was not
possible from that vantage. All quantitative HUD pipeline indicators were normal.

## Server-side exposure counters (from raw log, 2962 ticks, clients=1.0)

| Metric | Value |
|---|---|
| `exposure_applies_total` | **0** (across 2962 ticks) |
| `exposure n=` per 5 s window | 0 (all windows) |
| `exposure_mean` | 0.00 ms |
| `exposure_max` | 0.00 ms |
| `bcast_sends_total` | 5824 |
| `bcast_drops_total` | 0 |
| `bcast_timeouts_total` | 0 |

The exposure config was applied exactly once before `obscan_scan_start` (the
pre-scan apply, which is not counted by `exposure_applies_total`). Zero mid-session
re-applies across 2962 ticks.

## Checks

| # | Check | Result |
|---|---|---|
| 1 | `exposure_applies_total=0` throughout session (on-change fix active) | **[PASS]** `exposure_applies_total=0` across 2962 ticks in `[perf] summary` |
| 2 | `exposure n=0` in all [perf] windows with client connected | **[PASS]** `exposure n=0` in every window; confirmed in final 6 windows (clients=1.0) |
| 3 | Scan pipeline healthy: tracking SUCCESS | **[PASS]** `tracking: SUCCESS (0)` in HUD throughout observation |
| 4 | Point cloud accumulating normally | **[PASS]** 10,595 points, coverage 80%, accumulating per HUD |
| 5 | Data fps at server publish rate | **[PASS]** 10.0 fps client, 9.7 Hz server — consistent |
| 6 | No render errors or color corruption | **[PASS]** coordinator: "No color corruption, no missing bands, no render errors observed" |
| 7 | No broadcast drops | **[PASS]** `bcast_drops_total=0`, `bcast_timeouts_total=0` |
| 8 | No crash | **[PASS]** postflight: no crash report |

## Limitation

Direct visual inspection of the 3D point cloud was not possible in this observation
(orbit camera angle; geometry not in default view). The quantitative indicators
(point count accumulating, coverage increasing, tracking SUCCESS, fps healthy, no
render errors per HUD) are all consistent with a correctly rendering pipeline. A
direct side-by-side pixel comparison with and without the fix is not available from
this run.

## Crash sweep

`scripts/hw-postflight.sh plan151-93-153-session '2026-10-21 14:23:19' scan_server` →
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-21 14:23:19 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

## Open questions

- Would a direct before/after pixel comparison (with old per-tick re-assert vs new on-change) show any visible difference? The expected answer is no (the call is idempotent when the value is unchanged), but this run cannot confirm it directly.

## Board

- Row move: #153 `HW` → `DONE` — `exposure_applies_total=0` confirmed across 2962 ticks with a browser client connected; all HUD pipeline metrics normal (data 10.0 fps, render 60 fps, points accumulating, tracking SUCCESS, zero broadcast drops); no render errors per coordinator observation.
