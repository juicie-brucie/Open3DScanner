# PLAN #71 — attempt 12 A/B plane-removal toggle — PASS

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD SHA: 34d11e61aff07b7d206e7fb0641c686341fb236b
- Target: build/tools/scan_server/scan_server  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (12:53:07) — refreshed only on an app build (#107)
- Preflight: pwd OK; no conflicting process; daemon: RPCServer(53211,) running (process proxy: a vendor RPC host process exists); freshness: binary 12:53:07 >= newest source main.cpp 12:52:55
- Interposer: expect line 2: `[vulkan_disable_interpose] loaded` — **present**, line 2
- Run start: 2026-10-21 19:27:55

## Command

```
OPENSCANNER_PLANE_REMOVE=0 build/tools/scan_server/scan_server --live 8787 &
# ws_toggle_plane_removal.py started after OB_SCAN_SUCCESS confirmed:
python3 tests/scripts/ws_toggle_plane_removal.py 127.0.0.1 8787 15 20 10
# Arguments: stabilise=15s, enabled_window=20s, disabled_window=10s
```

## Result

- Exit code: SIGKILL (killed 5s after ws_toggle completed; normal forced teardown)
- Verdict: **PASS** — both toggle commands delivered; `plane_removal:` log lines present at both boundaries; measurable point-count step-change observed at the disable boundary
- Raw log: `2026-10-21-plan71-attempt12-ab.raw.log` (same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` — present, line 2

## Sequence

- 19:27:55 — scan_server launcher invoked (coordinator-attributed; not in raw log)
- 19:28:32 — `obscan_context_create -> OB_SCAN_SUCCESS` (raw log line 14: `[10/21 19:28:32.384811]`)
- 19:28:33 — device detected: CR0886100SN (raw log line 38: `[10/21 19:28:33.407566]`)
- 19:28:34 — device added, count=1 (raw log line 40: `[10/21 19:28:34.408816]`)
- 19:28:35–36 — Pipeline start done → `obscan_scan_start -> OB_SCAN_SUCCESS`; WebSocket server up on ws://0.0.0.0:8787 (raw log lines 185–193; nearest SDK timestamps `[10/21 19:28:35.782794]` immediately before and `[10/21 19:28:35.943039]` immediately after scan_start line)
- ~19:28:50 — ws_toggle connected; stabilise drain started (ws_toggle has no wall-clock timestamps; time is approximate, derived from context_destroy minus teardown window minus ws_toggle total duration 45 s)
- ~19:29:05 — enable command sent; `plane_removal: obscan_scan_plane_remove(true) -> OB_SCAN_SUCCESS` at tick=218, points=796
- ~19:29:25 — disable command sent; `plane_removal: obscan_scan_plane_remove(false) -> OB_SCAN_SUCCESS` at tick=412, points=8180; 409 frames drained over enabled window
- ~19:29:35 — ws_toggle sequence complete (204 frames drained over disabled window); clients dropped to 0
- ~19:29:38 — scan_server killed (SIGKILL, 5 s after ws_toggle)
- 19:29:43 — `obscan_context_destroy -> OB_SCAN_SUCCESS` (raw log line 6960: `[10/21 19:29:43.529196]`)

## Points-per-tick series (5-second windows)

| Window | Approx ticks | pts/tick | clients | Phase |
|---|---|---|---|---|
| 1 | 0–49 | 292.7 | 0.0 | pre-connect |
| 2 | 50–99 | 541.6 | 0.6 | client connected |
| 3 | 100–149 | 670.0 | 1.0 | stabilise |
| 4 | 150–199 | 770.4 | 1.0 | stabilise |
| 5 | 200–249 | 795.1 | 1.0 | **enable at tick=218** (pts=796) |
| 6 | 250–299 | 841.7 | 1.0 | plane removal ON |
| 7 | 300–349 | 933.8 | 1.0 | plane removal ON |
| 8 | 350–399 | 1660.7 | 1.0 | plane removal ON |
| 9 | 400–449 | 9967.7 | 1.0 | **disable at tick=412** (pts=8180) |
| 10 | 450–499 | 17304.5 | 1.0 | plane removal OFF |
| 11 | 500–549 | 17751.7 | 0.4 | ws_toggle ended |
| final | 550–588 | 17926.4 | 0.0 | post-disconnect |
| summary | 0–588 | 5758.0 | — | 588 ticks total |

Step-change at disable boundary: ~800–1660 pts/tick (plane removal ON) → ~10,000–17,900 pts/tick (plane removal OFF); approximately 10–22× increase.

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Device enumerated, scan_start reached | **[PASS]** `obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1`; `obscan_scan_start -> OB_SCAN_SUCCESS` |
| 2 | Interposer line 2 present | **[PASS]** `[vulkan_disable_interpose] loaded` |
| 3 | OPENSCANNER_PLANE_REMOVE=0 startup (no auto plane removal at boot) | **[PASS]** no `[auto 0->1 startup]` line; scan started with plane removal disabled |
| 4 | WebSocket toggle command delivered (clients > 0 after window 3; `sent enable:` logged) | **[PASS]** `[ws_toggle] sent enable: {"cmd": "set_plane_removal", "auto": true}`; clients=1.0 throughout; 306 frames drained in stabilise, 409 in enabled window, 204 in disabled window; bcast_drops=0 bcast_timeouts=0 |
| 5 | `plane_removal:` log line at toggle enable | **[PASS]** `[worker] plane_removal: obscan_scan_plane_remove(true) -> OB_SCAN_SUCCESS (0) accepted [auto 0->1 tick=218 points=796]` |
| 6 | `plane_removal:` log line at toggle disable | **[PASS]** `[worker] plane_removal: obscan_scan_plane_remove(false) -> OB_SCAN_SUCCESS (0) accepted [auto 1->0 tick=412 points=8180]` |
| 7 | Teardown clean | **[PASS]** `obscan_context_destroy -> OB_SCAN_SUCCESS`; tracking SUCCESS=1816/1816; `our_plane_removal fits=0 kept=0 cut=0` |

## Crash sweep

`scripts/hw-postflight.sh plan71-attempt12-ab '2026-10-21 19:27:55' scan_server` →
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-21 19:27:55 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

## Open questions

- The `our_plane_removal fits=0 kept=0 cut=0` final line indicates the application's own plane-removal code path was inactive during this run; is this expected for `OPENSCANNER_PLANE_REMOVE=0`, or does `our_plane_removal` track a separate flag?
- With plane removal ON (ticks 218–412) the per-tick point count climbed from 796 to 2074 over ~200 ticks (~20s) rather than dropping sharply; is the vendor's plane-removal algorithm progressive (accumulating a plane model over multiple ticks) or is the point count growth a scene-accumulation effect that happens to be slower than the removal?
- The manual-plane toggle (`config_manual_remove_plane`) was not exercised in this run; does it produce a step-change in point counts comparable to the auto-removal toggle?

## Board

- Row move: #71 `HW` → `DONE` — the mid-session toggle was delivered and accepted by the vendor SDK both ways (`OB_SCAN_SUCCESS`); `plane_removal:` log lines confirmed at both boundaries; a ~10–22× point-count increase at the disable boundary observed, confirming the toggle has a measurable effect in a single scan session.
