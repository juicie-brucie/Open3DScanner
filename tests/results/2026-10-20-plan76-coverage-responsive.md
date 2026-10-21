# PLAN #76 / #160 — coverage heatmap responsiveness re-observation — PASS (coverage IS responding to movement)

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (synced to origin/main @ `2174a66`)
- Target: build/tools/scan_server/scan_server (build-tree copy, fixed order 2: build/tools/<target>/<target>)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (22:17:05) — not used this run
- Preflight: `hw-preflight: OK`; pwd OK; no conflicting process; daemon RPCServer(26387,) running; freshness: binary 22:17:05 >= newest source `fast_encode.h` 22:06:30
- Interposer: expected `[vulkan_disable_interpose] loaded` (line 2) — present
- Run start: 2026-10-20 22:44:19
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`; enumeration `count=1` on first probe; scan started clean

## Note on prior aborted run (same session)

A prior attempt this session started scan_server on port 18899, which does not match the HUD's hardcoded WebSocket address (`ws://localhost:8787`). That run confirmed `clients=0.0` throughout — no browser WebSocket connection — so no coverage samples were obtainable. The server was stopped and restarted on the correct port (8787) for this run. The 18899 run is not separately recorded (no useful HUD data; server-side behaviour identical to the current run).

## Command

```
./build/tools/scan_server/scan_server --live 8787
```

Browser: user opened `http://localhost:8787` in browser; sampled `window.openScannerCoverage()` via console; values relayed by coordinator. User moved the scanner throughout the sampling window.

## Result

- Exit code: SIGKILL on worker (launcher had already received SIGTERM and paused; worker did not exit within 5 s of SIGTERM — force-killed; teardown logs show `obscan_scan_pause -> OB_SCAN_SUCCESS` but no destroy line)
- Verdict: **PASS** — coverage `visited` and `underCoveredPoints` respond to scanner movement; [hw-contradicted: 2026-10-20-plan76-coverage-frozen.md]
- Raw log: `2026-10-20-plan76-coverage-responsive.raw.log` (same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` — present, line 2

## Browser console samples (`window.openScannerCoverage()`, relayed by coordinator)

All samples: `resets=1`, `cell=0.015625` (consistent throughout)

| Elapsed | `visited` | `seen` | `underCoveredPoints` | Note |
|---|---|---|---|---|
| t+0s | 250 | 626 | 19840 | baseline, page just loaded |
| t+5s | 250 | 626 | 19840 | stale — identical to t+0s (page load window) |
| t+10s | 1109 | 1196 | 14393 | first responsive reading: `visited` 4.4× baseline |
| t+15s | 2047 | 1676 | 13789 | `visited` 8.2× baseline; `underCoveredPoints` down 30% |

## Server-side observations

| Metric | Value |
|---|---|
| Total FRAME_READY frames | 2720 |
| Tracking SUCCESS | 2523 (92.8%) |
| Tracking LOST | 198 (7.3%) |
| Browser WebSocket clients | `clients=1.0` (confirmed connected throughout movement window) |
| Points per tick window | ~19,300–19,870 (elevated vs pre-movement ~9,300, consistent with movement adding geometry) |
| Cloud change rate | 0.4–2.4 changes/s (active mesh updates during movement) |

## Checks

| # | Check | Result |
|---|---|---|
| 1 | Browser WebSocket connected (`clients > 0`) during sampling | **[PASS]** `clients=1.0` in all perf windows during the movement/sampling period |
| 2 | `visited` increases over time as scanner moves | **[PASS]** 250 → 250 → 1109 → 2047 across 15 s; nearly 10× increase from baseline by t+15s |
| 3 | `underCoveredPoints` decreases as more area is visited | **[PASS]** 19840 → 19840 → 14393 → 13789; down ~30% in 15 s of movement |
| 4 | `seen` climbs with movement (cloud growing) | **[PASS]** 626 → 626 → 1196 → 1676 |
| 5 | Coverage is NOT frozen (the #76/#160 owed re-observation after PR #413) | **[PASS]** — contradicts [hw-contradicted: 2026-10-20-plan76-coverage-frozen.md] |

## Contrast with the prior freeze finding

**Prior finding** (`2026-10-20-plan76-coverage-frozen.md`, [hw-contradicted by this run]):
- `visited=125`, `underCoveredPoints=6667` across six samples spanning ~25 s of movement
- `seen` climbed 176→3513, point count grew to `cloud_frames=1011`, yet `visited`/`underCoveredPoints` never changed
- Attributed to a bug in `ui/app.js`'s `coverageVisited`/`markVisited()` update trigger (row #160)

**This run** (post PR #413, which extracted coverage to `ui/coverage.js` and fixed the update trigger):
- `visited` climbed 250→2047 over 15 s of movement; `underCoveredPoints` dropped 19840→13789
- The coverage computation responds to movement as intended

**What this run does NOT prove**: the `visited` count at t+0s/t+5s was 250 (not 0), suggesting the coverage object had already partially computed from the prior session or page state before sampling began. Whether the very first frame of a fresh page load initialises correctly is not confirmed — both t+0s and t+5s readings were identical (stale), with the first responsive reading at t+10s. This is not observed to be a problem; it is noted as unconfirmed.

## Crash sweep

`scripts/hw-postflight.sh plan76-coverage-responsive '2026-10-20 22:44:19' scan_server` →
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-20 22:44:19 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

## Open questions

- The t+0s/t+5s readings were both `visited=250, seen=626, underCoveredPoints=19840` — identical and non-zero, suggesting the coverage object was already populated before sampling started (possibly from page-load computing an initial pose set). Does the coverage update fire on the very first WS frame, or only after subsequent frames arrive? Not a failure observation; noted for completeness.
- The recovery from 250 to 1109 (`visited`) happened between t+5s and t+10s. Was the user already moving at t+5s? If so, the 5 s lag before any change is visible may be an update-rate or batch-commit artefact in `ui/coverage.js`. Not diagnosed here.

## Board

- Row #160 `HW` → `DONE` — the coverage freeze (the row's filed symptom: `visited`/`underCoveredPoints` frozen while `seen` climbs and the scanner moves) is contradicted by this run; coverage responds to movement as expected post-PR #413.
- Row #76 `HW` → `DONE` — the owed re-confirmation after #160 is complete: the coverage HUD is live-responsive, `tracking` confirmed (2523/2720 SUCCESS), the red overlay's update mechanism is confirmed working (the `visited`/`underCoveredPoints` data that drives it is responding). The red overlay's visual appearance is not independently confirmed from this run (no screenshot was captured), but the underlying data feeding it is confirmed responsive.
