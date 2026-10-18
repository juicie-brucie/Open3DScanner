# PLAN #56 (stall-client survival) + #41 (backpressure) acceptance — scan_server side

**Date**: 2026-10-18
**Worktree**: `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`
**Branch / HEAD**: `hw/next` @ `4e52ffb155b8ca73da37deb071dc3da4b77c58e9`
**Target program**: `./build/tools/scan_server/scan_server --live 8787 "" 300`
**Raw log**: `tests/results/2026-10-18-stall-client-and-v5-visual.log`
**Spec**: `tests/hardware/plan56-stall-and-v5-visual.md`

**Scope note**: this subagent ran and observed the `scan_server` process only. The
browser-tab side of the procedure (tab A/B `backend` lines, the WebGPU-rejection path
exercise for #53, tab-side HUD continuity/disconnect observation) was explicitly the
main session's responsibility per its instructions to me, and is **not** reported here —
those facts belong in the main session's own record/addendum.

## Preconditions confirmed before the run

- `pwd` → `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`; branch `hw/next`. Correct
  worktree (has the interposer + 180s-class timeout machinery), not the main checkout.
- No other SDK-using process running: `ps aux | grep -iE "scan_server|live_capture_test|smoke_test|_message_listener_test|obscan"` returned nothing before the run started.
- Fix presence confirmed directly in source (more specific than the git-log check):
  ```
  tools/scan_server/main.cpp:593:    std::signal(SIGPIPE, SIG_IGN);
  engine/src/ws_broadcast_server.cpp:306:#if defined(SO_NOSIGPIPE)
  engine/src/ws_broadcast_server.cpp:311:            setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, ...);
  ```
- Binary freshness: `build/tools/scan_server/scan_server` mtime `Oct 18 17:00:35 2026`,
  newer than both `engine/src/ws_broadcast_server.cpp` and `tools/scan_server/main.cpp`
  (`Oct 18 16:02:18 2026`). Not rebuilt by me.

## Command and exit

```
./build/tools/scan_server/scan_server --live 8787 "" 300
```
Run started 17:04:49, worker exited 17:11:36 (teardown completed within the
session_seconds(300)+300=600s internal timeout budget, no watchdog fired).

- `EXIT_CODE=0`
- Launcher line: `[launcher] Worker exited normally with code 0.`

**Interposer-load confirmation (rule 6)**: stdout line 2 is
`[vulkan_disable_interpose] loaded` — the required observable for `scan_server --live`
(it sets `DYLD_INSERT_LIBRARIES` internally, so the usual `[launcher] DYLD_INSERT_LIBRARIES=...`
banner does not apply here). Confirmed present before trusting the run.

## Verdict: **PASS** (server-survival scope — #56)

The server did not crash under the stall; teardown was clean; exit code 0.

## Checks

| # | Check | Result |
|---|---|---|
| 1 | Interposer loaded (`[vulkan_disable_interpose] loaded`, line 2) | [PASS] |
| 2 | No `Broken pipe` / `signal 13` / `SIGPIPE` / `child timed out after` / `force-killed` anywhere in the 11,891-line log | [PASS] — grep found zero matches (exit code 1) |
| 3 | Tick-line cadence continuous through the whole session, no gap | [PASS] — 60 tick lines (0 through 2950, every 50), line-number spacing ~176–226 lines apart throughout, no irregular jump at any point including around the dip (see below) |
| 4 | `clients=` shows 2 (both tabs) at some point | [PASS] — clients=2 from tick 50 onward |
| 5 | `clients=` drop observed (intended prune behavior) | [PASS] — single-tick dip to `clients=1` at tick 250 |
| 6 | Process still alive / logging past the stall window | [PASS] — logging continues uninterrupted for 2700 more ticks after the dip, all the way to tick 2950 and into teardown |
| 7 | Clean teardown: pause → mesh → stop → destroy, all three teardown lines, exit 0 | [PASS] |
| 8 | #32 re-check: live point count grows normally, no stall in accumulation | [PASS] — see growth curve below |
| 9 | Crash report check (`~/Library/Logs/DiagnosticReports/`) for this run's window (17:04–17:11) | [PASS] — none found; most recent report in that directory predates the run (02:34 same day) |

## Tick-line continuity around the dip

Full list of every tick line's client count, in order (tick/3000, clients):
```
0  clients=0   <- startup
50 clients=2
100 clients=2
150 clients=2
200 clients=2
250 clients=1   <- the dip
300 clients=2   <- recovered, one tick later
350 clients=2
400 clients=2
... (clients=2 continuously for every tick from 300 through 2950, the last tick before teardown)
```

Raw log lines for the dip and its bracketing ticks (line numbers in the raw log):

```
954:[worker] tick 200/3000: color_cloud num_points=1184969 (max seen so far: 1195172), clients=2, paused=0
1155:[worker] tick 250/3000: color_cloud num_points=1235969 (max seen so far: 1235969), clients=1, paused=0
1353:[worker] tick 300/3000: color_cloud num_points=1258395 (max seen so far: 1258395), clients=2, paused=0
1560:[worker] tick 350/3000: color_cloud num_points=1278278 (max seen so far: 1278278), clients=2, paused=0
```

Line-number spacing between consecutive tick lines (proxy for even cadence — no
irregularity, including across the dip): 226, 176, 191, 196, 201, 198, 207, 190, 225...
— all in the same band; nothing resembling a multi-second hang in the logging itself at
any point, including straddling the dip.

Approximate wall-clock correlation (from nearby SDK-timestamped lines, since tick lines
themselves carry no timestamp): tick 200 region ≈ 17:05:25.31; tick 250/300 region is
bracketed by an SDK log at 17:05:31.42 shortly after. So the `clients=1` tick and its
recovery to `clients=2` both fall within roughly a 6-second window around 17:05:25–31,
consistent with a single ~3s stall-and-recover cycle. (I cannot attribute this precisely
to the main session's specific stall action without their own timing — reporting what
the log shows, not asserting causation.)

No explicit "client disconnected" / "client pruned" log line exists anywhere in the log
(grepped for `disconnect|prune|stall|dropped client|client removed|closing` — zero
matches outside of unrelated "frame will be dropped" SDK warnings) — the only server-side
observable of the prune/reconnect is the `clients=` count itself. This matches the spec's
framing that the drop is counted via tick lines, not a separate log message.

**Open point**: the `clients=` count dropped for exactly one tick (250) then recovered to
2 and stayed at 2 for the remainder of the run (2700 further ticks, to the end of the
session and through teardown). I cannot independently confirm from the server log alone
whether this was (a) tab A getting pruned and then reconnecting, or (b) a transient
one-sample miscount that never actually dropped the socket. Either reading is consistent
with "no crash, no stuck tick cadence" — the main session's tab-side observation (did tab
A show a disconnect/reconnect at all, and when) is needed to disambiguate, per the spec's
item 5 "if tab A reconnects ... record what it took."

## #32 re-check — point-count growth

No stall in accumulation; representative samples (tick: num_points, max seen):
```
tick 0:    num_points=0
tick 50:   num_points=151286
tick 100:  num_points=712385
tick 150:  num_points=1072418  (max 1195172)
tick 200:  num_points=1184969  (max 1195172)
tick 250:  num_points=1235969  (max 1235969)   <- continues growing through the dip tick
tick 300:  num_points=1258395
tick 900:  num_points=1332361  (max plateauing ~1,332,xxx-1,333,xxx from here on)
tick 2950: num_points=1332858  (max seen so far: 1333394)
```
Growth is monotonic-ish early on, plateaus near ~1.33M points by roughly tick 900 and
holds steady (expected scan-coverage saturation, not a stall) — consistent with prior
runs; not required to hit ≥1M here but it did (max 1,333,394).

## Teardown walkthrough

```
11819:[worker] obscan_scan_pause                             -> OB_SCAN_SUCCESS (0)
11820:[worker] obscan_scan_get_realtime_model_mesh -> OB_SCAN_SUCCESS (0), num_vertices=6910932, num_triangles=10884833
11821:[worker] Broadcast final reconstructed mesh preview (19974 of 6910932 points).
11822:[worker] Streamed full-detail mesh in 20000-point chunks.
11823:[worker] obscan_scan_stop                              -> OB_SCAN_SUCCESS (0)
11824:[worker] obscan_session_destroy poll 1: refused (OB_SCAN_ERROR_MULTI_CALLED)
11839:[worker] obscan_session_destroy poll 10: refused (OB_SCAN_ERROR_MULTI_CALLED)
11840:[worker] obscan_session_destroy poll 20: refused (OB_SCAN_ERROR_MULTI_CALLED)
...
11857:[worker] obscan_session_destroy poll 90: refused (OB_SCAN_ERROR_MULTI_CALLED)
11858:[worker] obscan_session_destroy: 98 poll(s), 97 refusal(s), 10.1s, final OB_SCAN_SUCCESS
11859:[worker] obscan_session_destroy                        -> OB_SCAN_SUCCESS (0)
11887:[worker] obscan_context_destroy: 1 poll(s), 0 refusal(s), 0.6s, final OB_SCAN_SUCCESS
11888:[worker] obscan_context_destroy                        -> OB_SCAN_SUCCESS (0)
11889:[worker] Frame protocol V1 counters: cloud_frames=3001 full_detail_chunks=346
11890:[launcher] Worker exited normally with code 0.
```

- `obscan_scan_pause`, `obscan_scan_stop` both immediate `OB_SCAN_SUCCESS`.
- `obscan_session_destroy`: 98 polls, 97 refusals (`OB_SCAN_ERROR_MULTI_CALLED`) over
  10.1s before final success — consistent with the usual asynchronous-teardown polling
  pattern seen in prior runs, not a new symptom.
- `obscan_context_destroy`: 1 poll, 0 refusals, 0.6s, success.
- All three teardown lines present; `[launcher] Worker exited normally with code 0.`
- No `child timed out after` / `force-killed` anywhere in the log.
- No leftover `scan_server` process after the run (`ps aux | grep scan_server` empty).

## Crash-report check (rule 11)

`ls -lt ~/Library/Logs/DiagnosticReports/` checked immediately after the run completed.
The most recent entry is `probe-2026-10-18-023423.ips` (02:34, same day, unrelated binary,
long before this run's 17:04–17:11 window). No entry for `scan_server` in this window.
Consistent with the spec's expectation that a SIGPIPE-suppressed run should not produce
one — no crash report to archive.

## Questions this run leaves open (for the main session / combined record)

1. Both tabs' `backend` lines (#53) — not observed by this subagent; main session's to
   record.
2. Whether tab A's single-tick `clients=1` dip corresponds to the main session's
   3-second JS-thread stall action, and whether tab A showed a visible disconnect /
   `Invalid frame header` and reconnect in its own UI — needs the main session's
   timing/observation to confirm correlation.
3. Tab B's continuity (did its HUD counter keep advancing without freeze) — not
   observable from the server log; main session's to confirm.
4. The WebGPU-rejection path exercise (#53 item 2b) — browser-side only, not run by this
   subagent.

## Main-session addendum: the four open questions, answered

**(1) `backend` lines, captured right after each tab connected:**
- Tab A (`http://127.0.0.1:8788/index.html?t=...`, no query): `backend: webgpu` — this
  browser has real WebGPU and the page used it live, by default, with no forced query
  param.
- Tab B (`http://127.0.0.1:8788/index.html?backend=webgl2&t=...`): `backend: webgl2` —
  the forced-fallback path renders correctly.

**(2) The `clients=1` dip correlates directly with the stall action**, timing-consistent
(the dip happened immediately after I ran the busy-loop, in the same ~6s window the
subagent independently bracketed from SDK timestamps). Tab A's own console showed the
same failure signature as the original crash run — `WebSocket connection to
'ws://127.0.0.1:8787/' failed` — but this time it **recovered**: within one page-text
read after the stall, tab A showed `status: connected`, `scan: scanning (live)`, and a
climbing frame counter again (its "Start/Pause" button toggled, consistent with a
reconnect resetting `latestStatus`). This answers the subagent's (a)/(b) question
directly: **(a) — tab A was pruned and reconnected**, not a miscount; the connection
genuinely dropped and came back.

**(3) Tab B never lost its stream.** Read at the same two checkpoints as tab A (right
after the stall, and again later): `status: connected`, `scan: scanning (live)`
throughout, frame counter climbing continuously across the stall window with no gap,
freeze, or disconnect at any point I observed.

**(4) The WebGPU-rejection path (#53 item 2b) was not exercised this run** — I captured
both the default (`webgpu`) and forced (`?backend=webgl2`) paths, but did not disable
WebGPU in the browser to confirm a rejecting `requestAdapter`/`requestDevice` falls
through to WebGL2 rather than showing "Neither WebGPU nor WebGL2 is available". That
specific sub-item of #53 remains open for a follow-up run.

**Final cross-check**: both tabs ended the session showing identical `points: 6910932`,
matching this record's final mesh `num_vertices=6910932` exactly — both clients received
and rendered the full-detail broadcast correctly, end to end.

**Overall verdict for #56/#41**: **PASS, unqualified.** The server survived the stall
that previously crashed it; the stalled client was pruned and reconnected cleanly
instead of taking down every client; the control tab was never affected. #53's `backend`
capability check also passes for the two paths exercised (default WebGPU, forced
WebGL2); only the rejection-path sub-item is still open.
