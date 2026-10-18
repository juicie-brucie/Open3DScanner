# PLAN row #32 — scan_server --live scale acceptance run

- Date: 2026-10-18
- Worktree: `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`
- Branch: `hw/next`
- Target: `build/tools/scan_server/scan_server`
- Binary mtime: 2026-10-18 01:17, same minute as `tools/scan_server/main.cpp` (01:17) — build looked fresh relative to source, not stale.
- Command:
  ```
  ./build/tools/scan_server/scan_server --live 8787 "" 120
  ```
- Wall clock: started 2026-10-18 05:20:14 UTC, process exited 05:22:47 UTC (~153s total, inside the ~240s+teardown budget for a 120s session + worker's own 240s internal timeout).
- Exit code: **0**
- Raw log: `tests/results/2026-10-18-scan-server-scale-acceptance.raw.log` (4559 lines, full stdout/stderr, unedited)

## Pre-run checks

- Confirmed `pwd` = `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`, branch `hw/next` (not the main checkout).
- `ps aux | grep -iE "scan_server|device_message_listener|orbbec"` → no output: no other SDK-using process was attached before starting (rule 7).
- No `pkill`/signal was sent; the process was allowed to run to its own completion and self-managed teardown.

## Banner check (rule 6)

Line 2 of the log:
```
[vulkan_disable_interpose] loaded
```
Line 1 is the launcher's own line (not the full `DYLD_INSERT_LIBRARIES=...` string — `scan_server` prints its own `[launcher] Running live scan server...` first line rather than the shared harness launcher banner). The `[vulkan_disable_interpose] loaded` line confirms the interposer was active, consistent with prior `scan_server --live` runs accepted under this same rule.

## Verdict: **FAIL (acceptance criterion not met in this run)** — server behaved correctly, but accumulated points did not reach the ≥1,000,000 bar

The server ran cleanly for its full budgeted duration with steady tick cadence, no stall, no crash, and clean teardown. However, **the per-tick `color_cloud num_points` never crossed 1,000,000 in this run** — it topped out at 306,327 at tick 1150/1200, and the final post-pause mesh snapshot broadcast 800,052 points. The row #32 acceptance bar ("UI stays live at ≥1M accumulated points, in a hardware run") was therefore **not demonstrated in this run**. This is very likely because this particular scan session did not cover/accumulate as much geometry as the earlier manual reference run (which reached 1,630,136 points by ~50s) — a function of how the device was moved during this session, not something the server code controls. I am reporting what was observed, not what was expected.

| Check | Result |
|---|---|
| Correct worktree/branch confirmed before run | [PASS] |
| No other SDK process attached (rule 7) | [PASS] |
| `[vulkan_disable_interpose] loaded` banner present (rule 6) | [PASS] |
| Process exits cleanly, exit code 0 | [PASS] |
| No hang / no kill needed (rule 8) | [PASS] |
| Tick cadence steady throughout (no multi-second stall) | [PASS] (see cadence analysis below) |
| `num_points` grows over the session | [PASS] (0 → 306,327 across ticks; final mesh 800,052) |
| `num_points` crosses 1,000,000 | **[FAIL]** — max observed 306,327 mid-session, 800,052 in final mesh |
| Clean pause/mesh/stop/destroy teardown | [PASS] |
| Crash report present for this run | N/A — none found (checked `~/Library/Logs/DiagnosticReports/`; no new file in the 05:20–05:23 UTC window; most recent entries predate this run by 2 days) |

## Per-tick log lines (verbatim, every 50th tick as printed)

```
[worker] tick 0/1200: color_cloud num_points=0 (max seen so far: 0), clients=0, paused=0
[worker] tick 50/1200: color_cloud num_points=168 (max seen so far: 168), clients=2, paused=0
[worker] tick 100/1200: color_cloud num_points=194 (max seen so far: 194), clients=2, paused=0
[worker] tick 150/1200: color_cloud num_points=197 (max seen so far: 197), clients=2, paused=0
[worker] tick 200/1200: color_cloud num_points=197 (max seen so far: 198), clients=2, paused=0
[worker] tick 250/1200: color_cloud num_points=196 (max seen so far: 198), clients=2, paused=0
[worker] tick 300/1200: color_cloud num_points=204 (max seen so far: 204), clients=2, paused=0
[worker] tick 350/1200: color_cloud num_points=203 (max seen so far: 205), clients=2, paused=0
[worker] tick 400/1200: color_cloud num_points=222 (max seen so far: 227), clients=2, paused=0
[worker] tick 450/1200: color_cloud num_points=323 (max seen so far: 323), clients=2, paused=0
[worker] tick 500/1200: color_cloud num_points=340 (max seen so far: 340), clients=2, paused=0
[worker] tick 550/1200: color_cloud num_points=341 (max seen so far: 342), clients=2, paused=0
[worker] tick 600/1200: color_cloud num_points=340 (max seen so far: 342), clients=2, paused=0
[worker] tick 650/1200: color_cloud num_points=6166 (max seen so far: 6166), clients=2, paused=0
[worker] tick 700/1200: color_cloud num_points=13494 (max seen so far: 13494), clients=2, paused=0
[worker] tick 750/1200: color_cloud num_points=19016 (max seen so far: 19016), clients=2, paused=0
[worker] tick 800/1200: color_cloud num_points=25626 (max seen so far: 25626), clients=2, paused=0
[worker] tick 850/1200: color_cloud num_points=40339 (max seen so far: 40339), clients=2, paused=0
[worker] tick 900/1200: color_cloud num_points=59167 (max seen so far: 59167), clients=2, paused=0
[worker] tick 950/1200: color_cloud num_points=103352 (max seen so far: 103352), clients=2, paused=0
[worker] tick 1000/1200: color_cloud num_points=145429 (max seen so far: 145429), clients=2, paused=0
[worker] tick 1050/1200: color_cloud num_points=149047 (max seen so far: 149047), clients=2, paused=0
[worker] tick 1100/1200: color_cloud num_points=200829 (max seen so far: 200829), clients=2, paused=0
[worker] tick 1150/1200: color_cloud num_points=306327 (max seen so far: 306327), clients=2, paused=0
```

No further `[worker] tick` lines appear after 1150/1200 before the session moved to pause/finalize (the process logged `obscan_scan_pause` shortly after, i.e. it appears the 120s session elapsed and pause began before a tick-1200 line was printed, or the print cadence every 50 ticks meant 1150 was the last multiple-of-50 line reached — see raw log for the exact transition at the line following tick 1150).

## Final mesh snapshot (after pause, before teardown)

```
[worker] obscan_scan_pause                             -> OB_SCAN_SUCCESS (0)
[worker] obscan_scan_get_realtime_model_mesh -> OB_SCAN_SUCCESS (0), num_vertices=800052, num_triangles=986076
[worker] Broadcast final reconstructed mesh as one point-cloud frame (800052 points).
```

800,052 is the final mesh vertex count, broadcast as one frame at the end — still short of 1,000,000, and distinct from the live per-tick `color_cloud num_points` series above.

## Tick cadence / stall analysis

`[worker] tick` lines themselves carry no embedded timestamp, so cadence was checked two ways:

1. **Line-count spacing between consecutive tick markers** (each tick's surrounding log volume, dominated by per-frame exposure-adjustment debug prints like `中心亮度:` blocks) stayed consistent at ~154–211 lines between every pair of the 50-tick-interval markers throughout the whole run — no point where this collapsed or spiked abnormally, which would be expected if ticking stalled.
2. **SDK log timestamps**: raw interleaved `[10/18 ...]` timestamps showed some apparent gaps up to ~32s (e.g. `01:21:17.219103` → `01:21:49.372520`), but these are an artifact of the SDK's own log rate-limiter, which batches repeated identical log lines and only emits a periodic summary — e.g. the line ending the 32s gap reads `[**72 logs in 48074ms, last: 01:21:47.488441**]`, i.e. that message type fired 72 times continuously over the window and the last occurrence was 1.9s before the printed summary, not a 32s silence. No gap in this run represents an actual stall of the worker loop.

Total elapsed wall time from first tick (`tick 0`, ~01:20:19) to last captured 50-multiple tick (`tick 1150`, ~01:22:29) is consistent with the requested 120s session length (1200 ticks × ~0.1s/tick ≈ 120s), i.e. the cadence tracks real time as expected, with no multi-second gap in the printed [worker] tick series itself (the 50-tick print granularity is too coarse to rule out sub-5s jitter, but nothing suggests a stall).

## Teardown (verbatim tail)

```
[worker] obscan_scan_pause                             -> OB_SCAN_SUCCESS (0)
[worker] obscan_scan_get_realtime_model_mesh -> OB_SCAN_SUCCESS (0), num_vertices=800052, num_triangles=986076
[worker] Broadcast final reconstructed mesh as one point-cloud frame (800052 points).
...
[10/18 01:22:45.511468][info][15240644][Pipeline.cpp:344] Try to stop pipeline!
[10/18 01:22:45.511468][info][15240644][Pipeline.cpp:316] Try to stop streams!
...
[10/18 01:22:45.585456][info][15240644][Pipeline.cpp:373] Stop pipeline done!
[10/18 01:22:45.585473][info][15240644][Pipeline.cpp:69] Pipeline destroyed! @0x7A7EF50798
[worker] obscan_scan_stop                              -> OB_SCAN_SUCCESS (0)
[worker] obscan_session_destroy                        -> OB_SCAN_SUCCESS (0)
...
[10/18 01:22:47.278415][info][15240644][Context.cpp:89] Context destroyed
[vulkan_disable_interpose] intercepted vkGetInstanceProcAddr lookup of a create function
[vulkan_disable_interpose] blocked vkCreateInstance
vkCreateInstance failed -3
[worker] obscan_context_destroy                        -> OB_SCAN_SUCCESS (0)
[launcher] Worker exited normally with code 0.
```

Teardown was clean: pause → final mesh snapshot → pipeline stop → scan stop → session destroy → context destroy → worker exits with code 0. No crash, no early kill, no multi-second hang.

## Crash report check (rule 11)

Checked `~/Library/Logs/DiagnosticReports/` for the run's time window (05:20–05:23 UTC / 01:20–01:23 local). No crash report was generated or found for this run; the most recent files there predate this run by two days (`live_capture_test`/`smoke_test` from 2026-10-16). This is expected given the clean exit code 0 and orderly teardown log.

## Open questions for the main session

1. **The server-side ≥1,000,000-point bar was not crossed in this hardware run** (max 306,327 live, 800,052 in the final mesh). If the main session's UI-side observation (HUD latency, data fps, render fps) was also only exercised up to ~306K/800K accumulated points this run, row #32's full acceptance (specifically "at ≥1M accumulated points") has **not yet been hardware-confirmed** — a rerun with more scan coverage (to accumulate >1M points within the session) would be needed to close that gap. The per-tick decimation fix itself showed no server-side cost blow-up as num_points grew through the hundreds-of-thousands range observed here (no stall, no FAIL), which is consistent with the fix working, but is not the same as confirming behavior at ≥1M points.
2. Minor: the prompt's banner-check wording (rule 6) describes the generic harness launcher line (`[launcher] DYLD_INSERT_LIBRARIES=...`); `scan_server`'s own first stdout line is `[launcher] Running live scan server in a worker process (up to 120s)...`, not that exact string. The confirming signal used here was `[vulkan_disable_interpose] loaded` on line 2, consistent with how prior `scan_server --live` runs (e.g. `2026-10-17-scan-server-live-acceptance.md`) were accepted — flagging for the record rather than treating as a blocker, since it matches established precedent for this specific tool.
