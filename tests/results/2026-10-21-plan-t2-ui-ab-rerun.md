# PLAN T2 — UI-open A/B plane-removal toggle (re-run with operator reads) — PASS

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (12:53:07) — refreshed only on an app build (#107)
- Preflight: pwd OK; no conflicting process; daemon: RPCServer(97382,) running (process proxy: a vendor RPC host process exists); freshness: binary 12:53:07 >= newest source main.cpp 12:52:55
- Interposer: expect line 2: [vulkan_disable_interpose] loaded  (scan_server sets DYLD_INSERT_LIBRARIES itself)
- Run start: 2026-10-21 21:56:09

## Command

```
OPENSCANNER_PLANE_REMOVE=0 build/tools/scan_server/scan_server --live 8787 &
# stdout+stderr → tests/results/2026-10-21-plan-t2-ui-ab-rerun.raw.log
python3 tests/scripts/ws_toggle_plane_removal.py 127.0.0.1 8787 30 30 30
# (waited 5s after ws_toggle completion)
kill 98908
```

## Result

- Exit code: `killed (SIGTERM)` — normal procedure termination
- Verdict: **PASS**
- Raw log: `2026-10-21-plan-t2-ui-ab-rerun.raw.log` (same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` — present at log line 2

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | obscan_context_create succeeds | **[PASS]** `[worker] obscan_context_create -> OB_SCAN_SUCCESS (0)` |
| 2 | Device detected | **[PASS]** `[worker] obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1` |
| 3 | obscan_scan_start succeeds | **[PASS]** line 162: `[worker] obscan_scan_start -> OB_SCAN_SUCCESS (0)` |
| 4 | WebSocket endpoint ready | **[PASS]** `[worker] Streaming live scan frames on ws://0.0.0.0:8787 (open ui/index.html)` |
| 5 | ws_toggle sequence complete | **[PASS]** stabilise 611 frames / enable 612 frames / disable 612 frames; "sequence complete" |
| 6 | plane_removal enable (auto 0->1) | **[PASS]** `[worker] plane_removal: obscan_scan_plane_remove(true) -> OB_SCAN_SUCCESS (0) accepted [auto 0->1 tick=647 points=544]` |
| 7 | plane_removal disable (auto 1->0) | **[PASS]** `[worker] plane_removal: obscan_scan_plane_remove(false) -> OB_SCAN_SUCCESS (0) accepted [auto 1->0 tick=938 points=575]` |
| 8 | #141 clients peak ≥ 2 | **[PASS]** perf windows show `clients=2.0` throughout ws_toggle window |
| 9 | #151 raw SDK FRAME_READY rate ~30/s | **[PASS]** tracking_status samples=3969; ticks=1305 at ~100ms/tick ≈ 130.5s; 3969/130.5 ≈ 30.4/s |
| 10 | bcast_drops / bcast_timeouts | **[PASS]** summary: `bcast_drops_total=0 bcast_timeouts_total=0` |
| 11 | #76 coverage % updated in HUD; red overlay present (OPERATOR READ) | **[PASS]** operator (browser screenshot at session end): coverage climbed ~70% → 91% covered, 192 under-covered pts, 12525 points total; red overlay present and updated as scanner moved |
| 12 | #93 markerless HUD line (OPERATOR READ) | **[N/A]** operator: no markerless stretch occurred; tracking showed `--` at disconnect (normal end state) — check not exercised this run |
| 13 | #153 picture stable throughout (OPERATOR READ) | **[PASS]** operator: picture stable throughout; point cloud is a lattice (expected for a still scan with no target object, not a stability failure) |

## Key Server-Side Metrics

- FRAME_READY tracking_status=0 (SUCCESS): **3969** total; tracking_status histogram: SUCCESS=3969
- Cloud frames: 1306 (frame protocol V1)
- Fused cloud tick rate: **9.7/s** (perf window `rate=` field)
- Tick rate nominal: 100.00 ms; mean tick: ~103 ms (tick_overshoot_mean=2.96 ms)
- tick_overshoot_max: 69.00 ms (single outlier)
- clients=2.0 confirmed during ws_toggle window
- bcast_sends_total: 4294; bcast_drops_total: 0; bcast_timeouts_total: 0
- plane_removal accepted at tick 647 (enable, 544 pts) and tick 938 (disable, 575 pts)

## Final Perf Summary

```
[perf] summary ticks=1305 mean_points=621.8 mean_bytes=7704.4 ... bcast_sends_total=4294 bcast_send_mean_us=19.04 bcast_send_max_us=9178 bcast_queue_max=1 bcast_drops_total=0 bcast_timeouts_total=0
```

## ws_toggle Output

```
[ws_toggle] connecting to ws://127.0.0.1:8787
[ws_toggle] connected
[ws_toggle] stabilise: drained 611 frames over 30s
[ws_toggle] sent enable: {"cmd": "set_plane_removal", "auto": true}
[ws_toggle] enabled-window: drained 612 frames over 30s
[ws_toggle] sent disable: {"cmd": "set_plane_removal", "auto": false}
[ws_toggle] disabled-window: drained 612 frames over 30s
[ws_toggle] sequence complete
```

## Crash Sweep

hw-postflight: no crash report for 'scan_server' at/after 2026-10-21 21:56:09 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.

## Raw Log

- File: `2026-10-21-plan-t2-ui-ab-rerun.raw.log` (15022 lines)
- sha256: `1ee7be0090b0c449e0ad28eac21f7b4a6c327946c45243db70a91d289d41cc1f`

## Open Questions

- #93 was not exercised this run (no markerless stretch occurred): is there a planned run or scenario that would reliably trigger markerless to gather the HUD-line evidence?
- tick_overshoot_max of 69.00 ms: is this consistent with the prior run's 30.70 ms outlier, or does it indicate a growing trend under this firmware build?

## Board

- Row move: #76 `HW` → `DONE` — operator confirmed (browser screenshot) coverage climbed ~70%→91%, 192 under-covered pts shown as red overlay, 12525 pts total; [hw-confirmed: 2026-10-21-plan-t2-ui-ab-rerun]
- Row move: #153 `HW` → `DONE` — operator confirmed picture stable throughout; lattice shape is expected for this scan target; [hw-confirmed: 2026-10-21-plan-t2-ui-ab-rerun]
- Row #93: leave as `HW` — markerless not triggered this run; evidence not yet gathered
- Row #141: already DONE per [hw-confirmed: 2026-10-21-plan-t2-ui-ab] — this run reconfirms clients=2.0
- Row #151: already DONE per [hw-confirmed: 2026-10-21-plan-t2-ui-ab] — this run reconfirms ~30.4/s
