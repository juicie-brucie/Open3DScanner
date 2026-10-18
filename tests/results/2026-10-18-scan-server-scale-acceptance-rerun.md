# PLAN row #32 — scan_server --live scale acceptance, rerun (300 s session)

- Date: 2026-10-18
- Worktree: `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`
- Branch: `hw/next`
- HEAD at run time: `2f61796` (`Merge pull request #87 from juicie-brucie/dispatch/land-recon4b`)
- Target: `build/tools/scan_server/scan_server`
- Binary mtime: 2026-10-18 01:37:30, newer than `tools/scan_server/main.cpp`'s 2026-10-18 01:17:23 — confirmed fresh build, no rebuild performed by this run.
- Command:
  ```
  ./build/tools/scan_server/scan_server --live 8787 "" 300
  ```
- Wall clock: started 2026-10-18 01:40:33 local, process exited 01:47:43 local (~430s total — inside the session_seconds+120=420s *internal* worker timeout, but note the worker itself *hit and was force-killed by* that 420s internal timeout; see Verdict).
- Exit code: **2**
- Raw log: `tests/results/2026-10-18-scan-server-scale-acceptance-rerun.raw.log` (12,183 lines, full stdout/stderr, unedited)

## Pre-run checks

- Confirmed `pwd` = `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`, branch `hw/next` (not the main checkout).
- `ps aux | grep -iE "scan_server|_message_listener_test"` → no output: no other SDK-using process was attached before starting (rule 7).
- No `pkill`/signal was sent by me at any point; the process was left to run to completion. (It was, in the end, force-killed — but by the tool's own internal 420 s watchdog, not by me. See Verdict.)

## Banner check (rule 6)

Line 1–2 of the log:
```
[launcher] Running live scan server in a worker process (up to 300s)...
[vulkan_disable_interpose] loaded
```
Same as both prior `scan_server --live` runs: this tool prints its own `[launcher] Running live scan server...` line rather than the shared harness's `DYLD_INSERT_LIBRARIES=...` string. `[vulkan_disable_interpose] loaded` on line 2 is the accepted substitute confirmation that the interposer was active, per established precedent (`2026-10-18-scan-server-scale-acceptance.md`, `2026-10-17-scan-server-live-acceptance.md`).

## Verdict: **FAIL** — the ≥1,000,000-point bar *was* crossed live, but the run did not end cleanly: exit code 2, worker force-killed by its own 420 s timeout after teardown's logical steps had already completed

Two independent things happened in this run and they need to be reported separately:

1. **The point-count bar was reached and sustained.** Live `color_cloud num_points` crossed 1,000,000 at some point between tick 400 (991,963) and tick 450 (1,195,834) — i.e. roughly 40–45 s into the session — and stayed at or above ~1,000,000 for the rest of the live-tick series, climbing to a **max live value of 1,412,644** at tick 2950 (the last tick line printed). This closes the gap the first (120 s) run left open.
2. **The run itself did not exit cleanly.** After the live session, `obscan_scan_pause`, `obscan_scan_get_realtime_model_mesh` (15,586,663 vertices), the final-mesh broadcast, and the full pipeline stop/destroy sequence (`Pipeline.cpp: Stop pipeline done!` / `Pipeline destroyed!`) all completed and logged success. Immediately after, the worker printed `rotate pose for model.ply` and a 4x4 matrix — and then produced **no further output**. No `obscan_scan_stop`, no `obscan_session_destroy`, no `obscan_context_destroy`, no `[launcher] Worker exited normally` line ever appeared (compare the clean teardown tail in the first run's record, which has all four of those lines). The launcher's own internal 420 s watchdog fired and force-killed the child:
   ```
   [launcher] Worker crashed or timed out inside the vendor SDK (child timed out after 420s and was force-killed).
   ```
   Exit code was **2**, not 0.

Per the procedure's own verdict rule ("PASS = ... no multi-second tick stalls, clean teardown, exit 0"), this run is a **FAIL** on the teardown/exit-code leg even though the data-scale leg succeeded. This is a genuinely new finding relative to the first run — the first (120 s) run tore down cleanly with exit 0; this longer, larger-point-count run hung somewhere after mesh broadcast and pipeline destroy, in whatever step follows "rotate pose for model.ply" (reads like the PLY-file write step), and needed the tool's own built-in timeout to recover. I did not retry this run or intervene (rule 8); I am reporting it as found.

| Check | Result |
|---|---|
| Correct worktree/branch confirmed before run | [PASS] |
| No other SDK process attached (rule 7) | [PASS] |
| `[vulkan_disable_interpose] loaded` banner present (rule 6) | [PASS] |
| Live `color_cloud num_points` crosses 1,000,000 during ticking (not just final mesh) | [PASS] — crossed between tick 400 (991,963) and tick 450 (1,195,834); max live 1,412,644 at tick 2950 |
| Tick cadence steady, no multi-second stall, during the live session | [PASS] (see cadence analysis below; no evidence of a stall while ticks were printing) |
| Clean pause → mesh → stop → destroy → process-exit teardown | **[FAIL]** — pause, mesh snapshot, and pipeline stop/destroy all logged success, but the process then stalled after printing `rotate pose for model.ply` and never logged `obscan_scan_stop`/`obscan_session_destroy`/`obscan_context_destroy`/worker-exit; it had to be force-killed by the tool's own 420 s internal timeout |
| Exit code 0 | **[FAIL]** — exit code was 2 |
| No kill/hang needed (rule 8) | **[FAIL]** — the worker hung and was auto-killed by its own internal watchdog (not by me); I did not retry |
| Crash report present for this run | None found — checked `~/Library/Logs/DiagnosticReports/`; most recent entries are from 2026-10-17 22:45 (`device_message_listener_test`), well before this run's 01:40–01:48 window. A force-kill of a hung process does not generally produce a macOS crash report (no signal/trap from the OS, just a deliberate SIGKILL by the watchdog), consistent with finding none. |

## Per-tick log lines (representative sampling, ~every 15 s of session time = every 3rd 50-tick marker; full series of 60 lines is in the raw log)

```
[worker] tick 0/3000:    color_cloud num_points=0       (max seen so far: 0),       clients=0, paused=0
[worker] tick 150/3000:  color_cloud num_points=172059   (max seen so far: 172059),  clients=3, paused=0
[worker] tick 300/3000:  color_cloud num_points=914877   (max seen so far: 914877),  clients=3, paused=0
[worker] tick 450/3000:  color_cloud num_points=1195834  (max seen so far: 1195834), clients=3, paused=0   <- crosses 1,000,000
[worker] tick 600/3000:  color_cloud num_points=1120923  (max seen so far: 1231692), clients=3, paused=0
[worker] tick 750/3000:  color_cloud num_points=1225607  (max seen so far: 1231692), clients=3, paused=0
[worker] tick 900/3000:  color_cloud num_points=1263313  (max seen so far: 1267968), clients=3, paused=0
[worker] tick 1050/3000: color_cloud num_points=1300011  (max seen so far: 1300011), clients=3, paused=0
[worker] tick 1200/3000: color_cloud num_points=1295534  (max seen so far: 1301323), clients=3, paused=0
[worker] tick 1350/3000: color_cloud num_points=1326866  (max seen so far: 1326866), clients=3, paused=0
[worker] tick 1500/3000: color_cloud num_points=1335317  (max seen so far: 1335760), clients=3, paused=0
[worker] tick 1650/3000: color_cloud num_points=1341579  (max seen so far: 1341720), clients=3, paused=0
[worker] tick 1800/3000: color_cloud num_points=1346794  (max seen so far: 1347733), clients=3, paused=0
[worker] tick 1950/3000: color_cloud num_points=1357397  (max seen so far: 1357428), clients=3, paused=0
[worker] tick 2100/3000: color_cloud num_points=1366382  (max seen so far: 1366902), clients=3, paused=0
[worker] tick 2250/3000: color_cloud num_points=1376418  (max seen so far: 1376889), clients=3, paused=0
[worker] tick 2400/3000: color_cloud num_points=1380190  (max seen so far: 1380737), clients=3, paused=0
[worker] tick 2550/3000: color_cloud num_points=1382408  (max seen so far: 1382408), clients=3, paused=0
[worker] tick 2700/3000: color_cloud num_points=1393217  (max seen so far: 1394375), clients=3, paused=0
[worker] tick 2850/3000: color_cloud num_points=1407653  (max seen so far: 1407653), clients=3, paused=0
[worker] tick 2950/3000: color_cloud num_points=1412644  (max seen so far: 1412644), clients=3, paused=0   <- last live tick printed
```

### Last ~30 s of the live series (every 50-tick line, last ~300 ticks ≈ 30 s)

```
[worker] tick 2700/3000: color_cloud num_points=1393217 (max seen so far: 1394375), clients=3, paused=0
[worker] tick 2750/3000: color_cloud num_points=1400541 (max seen so far: 1400541), clients=3, paused=0
[worker] tick 2800/3000: color_cloud num_points=1404095 (max seen so far: 1404095), clients=3, paused=0
[worker] tick 2850/3000: color_cloud num_points=1407653 (max seen so far: 1407653), clients=3, paused=0
[worker] tick 2900/3000: color_cloud num_points=1410073 (max seen so far: 1410359), clients=3, paused=0
[worker] tick 2950/3000: color_cloud num_points=1412644 (max seen so far: 1412644), clients=3, paused=0
```
No `tick 3000/3000` line was ever printed — the 300 s session appears to have ended (pause triggered) in the gap between tick 2950 and the next 50-tick print.

### All readings ≥ 1,000,000 (count)

Every 50-tick marker from tick 450 (1,195,834) through tick 2950 (1,412,644) — 52 consecutive printed samples — was at or above 1,000,000, with one brief dip pattern right after the initial crossing (tick 450: 1,195,834 → tick 500: 1,073,047 → tick 550: 1,091,604 → tick 600: 1,120,923) that stayed above 1,000,000 throughout; the count never dropped back under the bar once it crossed.

### Notable plateau (flagged per procedure, likely operator repositioning, not a server issue)

- Ticks 1100–1250 (~110–125 s into the session) hovered in a narrow band — 1,301,230 → 1,297,683 → 1,295,534 → 1,297,840 → 1,308,866 — essentially flat/slightly oscillating for ~75 s of wall time, rather than climbing.
- Ticks 2450 and 2500 show the *exact same* value, 1,380,937 — a 25 s window (5 s × 5 ticks of 50-step gap... actually 50 ticks = one line-to-line gap here, i.e. ~5s nominal, but see cadence note below on actual elapsed time) with zero net new points.
- Both are consistent with the procedure's note that fusion only grows while new geometry enters the field of view — these look like periods between repositioning, not a tick-cadence or server problem (ticks kept printing on schedule through both).

## Max live `color_cloud` count and final post-pause mesh count

- **Max live `color_cloud num_points`: 1,412,644** (instantaneous, tick 2950) / **max-seen-so-far counter: 1,412,644** at the same tick — both exceed the 1,000,000 bar.
- **Final post-pause mesh snapshot: `num_vertices=15,586,663`, `num_triangles=26,447,468`**, broadcast once as a single point-cloud frame of 15,586,663 points:
  ```
  [worker] obscan_scan_pause                             -> OB_SCAN_SUCCESS (0)
  [worker] obscan_scan_get_realtime_model_mesh -> OB_SCAN_SUCCESS (0), num_vertices=15586663, num_triangles=26447468
  [worker] Broadcast final reconstructed mesh as one point-cloud frame (15586663 points).
  ```
  This final mesh count (15.6M) is an order of magnitude above the live per-tick decimated count (1.41M max), consistent with the first run's pattern that the final mesh is a denser, non-decimated snapshot distinct from the live `color_cloud` series.

## Tick cadence analysis (timestamps cross-referenced against nearby SDK log lines; `[worker] tick` lines themselves carry no timestamp)

Using the nearest preceding timestamped SDK log line for several tick markers:

| Tick | Approx. wall-clock | Elapsed from tick 0 |
|---|---|---|
| 0 | 01:40:38 | 0 s |
| 500 | 01:41:33 | 55 s |
| 1000 | 01:42:33 | 115 s |
| 1500 | 01:43:31 | 173 s |
| 2000 | 01:44:09 | 211 s |
| 2450 | 01:45:22 | 284 s |
| 2950 | 01:46:10 | 332 s |

The live-tick portion covered 2950 ticks in ~332 s of wall time (≈0.1125 s/tick average), somewhat slower than the nominal 0.1 s/tick (3000 ticks ÷ 300 s) the session parameter implies, but with no single large jump in this table that would indicate a multi-second stall — the per-500-tick intervals ranged ~38–73 s, i.e. mildly uneven but not collapsing. I did not find a gap consistent with a stall anywhere in the printed tick series while it was printing. The actual hang (see Verdict) happened *after* the last tick and after teardown's logged steps, not during live ticking.

## Teardown (verbatim tail of the raw log)

```
[worker] obscan_scan_pause                             -> OB_SCAN_SUCCESS (0)
[10/18 01:46:42.891238][warning][...][Pipeline.cpp:310] Wait for frame timeout, you can try to increase the wait time! current timeout=100
... (frame-drop / GVCP warnings while idle, ~45s) ...
[worker] obscan_scan_get_realtime_model_mesh -> OB_SCAN_SUCCESS (0), num_vertices=15586663, num_triangles=26447468
[worker] Broadcast final reconstructed mesh as one point-cloud frame (15586663 points).
[10/18 01:47:29.027453][info][15272812][RPCClientPort.cpp:381] Stop imu stream error: std::bad_cast
[10/18 01:47:29.030881][info][15272812][Pipeline.cpp:344] Try to stop pipeline!
[10/18 01:47:29.030898][info][15272812][Pipeline.cpp:316] Try to stop streams!
[10/18 01:47:29.067669][info][15272812][RPCClientPort.cpp:255] stop OB_STREAM_IR_RIGHT stream call
[10/18 01:47:29.070212][info][15272812][RPCClientPort.cpp:265] stop OB_STREAM_IR_RIGHT stream done
[10/18 01:47:29.071103][info][15272812][VideoSensor.cpp:734] Video sensor stopped, @OB_SENSOR_IR_RIGHT
[10/18 01:47:29.071114][info][15272812][Pipeline.cpp:329] Sensor stream stopped, sensorType=OB_SENSOR_IR_RIGHT
[10/18 01:47:29.101040][info][15272812][RPCClientPort.cpp:255] stop OB_STREAM_IR_LEFT stream call
[10/18 01:47:29.106279][info][15272812][RPCClientPort.cpp:265] stop OB_STREAM_IR_LEFT stream done
[10/18 01:47:29.111069][info][15272812][VideoSensor.cpp:734] Video sensor stopped, @OB_SENSOR_IR_LEFT
[10/18 01:47:29.111084][info][15272812][Pipeline.cpp:329] Sensor stream stopped, sensorType=OB_SENSOR_IR_LEFT
[10/18 01:47:29.111094][info][15272812][Pipeline.cpp:335] Stop streams done!
[10/18 01:47:29.111111][info][15272812][Pipeline.cpp:373] Stop pipeline done!
[10/18 01:47:29.111396][info][15272812][Pipeline.cpp:69] Pipeline destroyed! @0x77F330C018
[10/18 01:47:29.299310][warning][...][Pipeline.cpp:310] Wait for frame timeout, you can try to increase the wait time! current timeout=100 [**12 logs in 6101ms**]
rotate pose for model.ply
-0.000000044, -1.000000000, 0.000000000, 0.000000000
1.000000000, -0.000000044, 0.000000000, 0.000000000
0.000000000, 0.000000000, 1.000000000, 0.000000000
0.000000000, 0.000000000, 0.000000000, 1.000000000

[launcher] Worker crashed or timed out inside the vendor SDK (child timed out after 420s and was force-killed).
EXIT_CODE=2
```

**This is not a clean teardown.** `Pipeline destroyed!` and the rotate-pose/model.ply matrix print are the last things the worker logged. There is no `obscan_scan_stop`, no `obscan_session_destroy`, no `obscan_context_destroy`, and no `[launcher] Worker exited normally with code 0` — all four of which are present in the first run's clean-teardown tail. Something in the step after printing the rotate-pose matrix (plausibly the PLY file write, or whatever SDK call follows it) did not return, and the worker sat there until the launcher's own internal 420 s timeout killed it.

## Crash report check (rule 11)

Checked `~/Library/Logs/DiagnosticReports/` for this run's window (01:40–01:48 local, 2026-10-18). No crash report was found for `scan_server` or any related process; the most recent files in that directory are `device_message_listener_test-2026-10-17-224548.ips` and earlier, from the previous day. A force-kill by an internal watchdog (SIGKILL from the launcher, not a hardware/OS-level trap) is consistent with producing no macOS crash report, so this absence is expected rather than a gap in evidence — but per rule 11 it was explicitly checked, not assumed.

## Main-session addendum: UI-side corroboration

While this run was in progress, the main session watched the live HUD (via a
plain static file server serving `ui/index.html` — not an SDK action) and the
user physically moved the scanner for the session's duration. Observed from the
browser side:

- `render` held steady at ~60fps throughout the live-scanning phase (the HUD's
  displayed `points` value itself plateaus near the 20,000-point broadcast
  budget once the real count exceeds it — a display artifact of the decimation
  fix, not a sign of stalled accumulation; the authoritative count is this
  record's server-side `color_cloud num_points` series above).
- A screenshot taken at the moment of disconnect (session end) shows
  `points: 15,586,663`, `frame: #3000`, `render: 29.2 fps` (a transient dip
  rendering the single large uncapped final-mesh frame, not a live-phase
  reading) — the displayed count matches this record's final mesh
  (`num_vertices=15,586,663`) exactly, cross-confirming the server-side figure.
  Archived at
  [`2026-10-18-scan-server-scale-acceptance-rerun-final-hud.jpg`](./2026-10-18-scan-server-scale-acceptance-rerun-final-hud.jpg).

## Open questions for the main session

1. **The ≥1,000,000-point scale bar is now hardware-confirmed live** (crossed ~tick 400–450, sustained through 1,412,644 at tick 2950) — closing the gap the first (120 s) run left open. If the HUD (watched in the main session) showed usable `data`/`render` fps through that range, the data-scale half of row #32's acceptance criterion has real evidence behind it now.
2. **New problem, not previously seen**: this longer/larger run did not exit cleanly. It got through pause, final-mesh extraction, and pipeline stop/destroy, then stalled at (or just after) `rotate pose for model.ply` and had to be force-killed by its own 420 s internal timeout — exit code 2, not 0. This looks size- or duration-dependent (the first, 120 s/800K-point run tore down cleanly with exit 0; this 300 s/15.6M-vertex-mesh run did not) — possibly the PLY-write step scales with mesh size (15.6M vertices here vs. 800K previously) and is slow or stuck at this scale. This needs investigation as a build/code finding, not another hardware rerun by me — I did not touch the code per the rules I operate under.
3. Per rule 8, since the worker did hang and required a forced kill (even though it was the tool's own internal watchdog, not a `pkill` from me), I am stopping here and not attempting another run. If a rerun is wanted to isolate whether the hang is reproducible or tied to mesh size/PLY-write, that decision belongs to the main session/user, not to this subagent.
