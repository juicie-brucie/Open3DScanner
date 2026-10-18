# scan_server --live scale + teardown acceptance (#32 / #39 / #43 / #45)

- Date: 2026-10-18
- Worktree: `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`
- Branch at run time: `hw/next` @ `0392ed8` (merge of PR #115, "build/plan45-protocol-log"; also includes PR #111 non-blocking-teardown)
- Binary: `build/tools/scan_server/scan_server` (mtime 2026-10-18 03:00, same minute as `tools/scan_server/main.cpp` — not stale; not rebuilt)
- Target program: `scan_server --live`
- Command:
  ```
  ./build/tools/scan_server/scan_server --live 8787 "" 300
  ```
- Exit code: **0** (`[launcher] Worker exited normally with code 0.` / `EXIT_CODE=0`)
- Raw log: `tests/results/2026-10-18-scan-server-scale-teardown-acceptance.log` (12,859 lines)
- No concurrent SDK user: `ps aux` showed no other `scan_server`/`device_message_listener`/orbbec process running before this run started (rule 7 honored). Main session's `ui/watch.sh` static file server + browser WS client are not SDK users, per task framing.
- Crash reports: checked `~/Library/Logs/DiagnosticReports/` after the run. Only file present is `probe-2026-10-18-023423.ips`, timestamped **before** this run started (run window was ~03:01:43–03:09:00) and unrelated to `scan_server`. No crash report exists for this run (consistent with its clean exit 0).

## Banner check (rule 6) — a caveat, reported explicitly

The log does **not** contain a line starting with `[launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib` anywhere (`grep -n "DYLD_INSERT_LIBRARIES"` on the full log returns nothing). The dispatching instructions for this specific run explicitly named the substitute check for `scan_server --live` runs: `[vulkan_disable_interpose] loaded`, which **is** present as the second line of stdout:
```
[launcher] Running live scan server in a worker process (up to 300s)...
[vulkan_disable_interpose] loaded
[worker] Loading OBScan SDK from: /Applications/CrealityScan.app/Contents/PlugIns/liborbbec_scan.dylib
```
I'm flagging the absence of the literal rule-6 banner line rather than silently substituting — this matches the pattern of prior `scan_server --live` runs per the dispatch note, but strictly it is a deviation from the hard-rule wording, so it's on record here rather than glossed over.

## Verdict

**PASS for #32, #39, #45. #43 is PASS on (a), (c), (d), (e); (b) is only indirectly supported, not directly evidenced in the log — see the detailed walkthrough below. Do not read this as an unqualified #43 pass.**

| # | Check | Result |
|---|---|---|
| 1 | Launcher substitute banner `[vulkan_disable_interpose] loaded` present | [PASS] |
| 2 | Rule-6 literal `DYLD_INSERT_LIBRARIES=` banner present | [FAIL] (absent; see caveat above — consistent with prior runs of this binary per dispatch note) |
| 3 | #32: live `color_cloud num_points` crosses and sustains ≥1,000,000 | [PASS] |
| 4 | #39: full-detail mesh streamed automatically after pause (`full_mesh`-style chunking) | [PASS] |
| 5 | #43(a): `obscan_scan_stop` returns immediately (non-blocking) | [PASS] |
| 6 | #43(b): destroys refuse with `MULTI_CALLED`-style status, polled, until tail completes | [PARTIAL / NOT DIRECTLY EVIDENCED] |
| 7 | #43(c): all three teardown calls present, each eventually `SUCCESS` | [PASS] |
| 8 | #43(d): exit code 0 | [PASS] |
| 9 | #43(e): no watchdog fire (`child timed out after`, `force-killed`) | [PASS] |
| 10 | #45: `Frame protocol V1 (envelope: ...)` startup line present | [PASS] |
| 11 | #45: `Frame protocol V1 counters: ...` teardown summary line present | [PASS] |

## 1. #32 — live point count re-confirmed at scale

Per-tick series sampled every 50 ticks (the tool emits a tick line roughly every ~2.9s of wall time; 3000 ticks total over the 300s session), quoted verbatim:

```
[worker] tick 0/3000: color_cloud num_points=0 (max seen so far: 0), clients=0, paused=0
[worker] tick 50/3000: color_cloud num_points=33974 (max seen so far: 33974), clients=2, paused=0
[worker] tick 100/3000: color_cloud num_points=418713 (max seen so far: 418713), clients=2, paused=0
[worker] tick 150/3000: color_cloud num_points=745196 (max seen so far: 745196), clients=2, paused=0
[worker] tick 200/3000: color_cloud num_points=1056776 (max seen so far: 1056776), clients=2, paused=0   <- crosses 1,000,000
[worker] tick 250/3000: color_cloud num_points=1068738 (max seen so far: 1258741), clients=2, paused=0
...
[worker] tick 2850/3000: color_cloud num_points=1430563 (max seen so far: 1430563), clients=2, paused=0
[worker] tick 2900/3000: color_cloud num_points=1431656 (max seen so far: 1431890), clients=2, paused=0
[worker] tick 2950/3000: color_cloud num_points=1432711 (max seen so far: 1432711), clients=2, paused=0
```

The count crosses 1,000,000 by tick 200 (roughly the 20–30s mark of session time) and sustains above it for the entire remainder of the 300s session, right through the last ~30s before pause. **Maximum live count observed: 1,432,711** (final tick before pause), with the running "max seen so far" counter reaching the same value. This re-confirms #32 at scale, consistent with and exceeding the earlier 15.6M-vertex-run reconfirmation bar.

## 2. #39 — full-detail mesh streamed automatically after pause

```
[worker] obscan_scan_get_realtime_model_mesh -> OB_SCAN_SUCCESS (0), num_vertices=16659778, num_triangles=28819752
[worker] Broadcast final reconstructed mesh preview (20000 of 16659778 points).
[worker] Streamed full-detail mesh in 20000-point chunks.
```

Final mesh: **16,659,778 vertices, 28,819,752 triangles**. This build has no separate client-request message for the full-detail path (per the task framing) — streaming kicked off automatically right after the `obscan_scan_get_realtime_model_mesh` call succeeded, chunked at 20,000 points per chunk, and the `Streamed full-detail mesh in 20000-point chunks.` line confirms completion server-side. (UI responsiveness during this chunking is out of scope for this report per the dispatch note — that's the main session's observation to make.)

## 3. #43 — teardown acceptance, point by point

**(a) Does `obscan_scan_stop` return immediately (not blocking for the SDK's serialization tail)? → PASS.**

```
[worker] obscan_scan_pause                             -> OB_SCAN_SUCCESS (0)
... (frame timeouts, GVCP noise continue for ~29s while paused, as the device keeps idling)
[10/18 03:08:27.367926][warning][...][Pipeline.cpp:310] Wait for frame timeout...
[worker] obscan_scan_get_realtime_model_mesh -> OB_SCAN_SUCCESS (0), num_vertices=16659778, num_triangles=28819752
[worker] Broadcast final reconstructed mesh preview (20000 of 16659778 points).
[worker] Streamed full-detail mesh in 20000-point chunks.
[10/18 03:08:32.331002][warning][...] Wait for frame timeout...
[10/18 03:08:33.617115][warning][...] This frame will be dropped... @OB_SENSOR_IR_RIGHT
[10/18 03:08:33.618154][warning][...] This frame will be dropped... @OB_SENSOR_IR_LEFT
[worker] obscan_scan_stop                              -> OB_SCAN_SUCCESS (0)
[10/18 03:08:36.249218][info][15402230][RPCClientPort.cpp:381] Stop imu stream error: std::bad_cast
[10/18 03:08:36.252697][info][15402230][Pipeline.cpp:344] Try to stop pipeline!
[10/18 03:08:36.252737][info][15402230][Pipeline.cpp:316] Try to stop streams!
...
[10/18 03:08:36.287337][info][15402230][Pipeline.cpp:335] Stop streams done!
[10/18 03:08:36.287357][info][15402230][Pipeline.cpp:373] Stop pipeline done!
[10/18 03:08:36.287556][info][15402230][Pipeline.cpp:69] Pipeline destroyed! @0x7CE12B0618
```

The worker's `obscan_scan_stop -> OB_SCAN_SUCCESS` print appears before the SDK's own pipeline-teardown logs (`Try to stop pipeline!` through `Pipeline destroyed!`), and that whole pipeline-level teardown completes in **~35ms** (03:08:36.252697 → 03:08:36.287556). The source confirms `is_block=false` is passed explicitly: `tools/scan_server/main.cpp:788: status = lib.obscan_scan_stop(scan, &OnPreviewProgress, &OnPreviewStatus, /*is_block=*/false);` with the accompanying comment "`is_block=false` returns immediately: the SDK runs the save/serialize tail on its 'default' pool instead of blocking this thread." The observed fast pipeline-stop return, with a much longer out-of-core serialization tail following afterward (see (b)), is consistent with this.

**(b) Do `obscan_session_destroy`/`obscan_context_destroy` refuse with a `MULTI_CALLED`-style status while the tail runs, polling until it completes? → NOT DIRECTLY EVIDENCED in the log; only indirect timing support. Flagging this rather than calling it a clean pass.**

Grepping the full raw log for `MULTI_CALLED` (any case) and for repeated destroy-call lines returns **nothing** beyond a single call-then-success line each:
```
[worker] obscan_session_destroy                        -> OB_SCAN_SUCCESS (0)
[worker] obscan_context_destroy                        -> OB_SCAN_SUCCESS (0)
```
That is exactly the "single call-then-success" pattern the dispatch explicitly said not to accept as proof of (b). However, reading `tools/scan_server/main.cpp` (permitted — it's the test harness, not vendor/under-test code, and I only read it, did not edit it) shows the polling loop **does exist in source**:
```cpp
for (int i = 0; i < 3000; ++i) {
    status = lib.obscan_session_destroy(&session);
    if (status == OB_SCAN_SUCCESS || status != OB_SCAN_ERROR_MULTI_CALLED) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
Report("obscan_session_destroy", status);
```
and identically for `obscan_context_destroy`. Critically, `Report(...)` is only called **once, after the loop exits** — the harness does not log each individual poll attempt, by design. So the raw log, by construction, cannot show "repeated destroy-call log lines with a non-success status" even if the polling loop ran hundreds of times; the per-iteration status is never printed. This is a logging-granularity gap, not proof either way.

The only evidence I can offer is indirect/timing-based: between the SDK's own `Pipeline destroyed!` line (03:08:36.287556, part of the fast scan_stop pipeline teardown) and the next SDK-internal line, `Pioneer device destroyed!` (03:08:59.823659), there is a **23.536-second gap**, during which the only output is the worker's model-serialization print (`rotate pose for model.ply` + a 4x4 transform matrix) followed immediately by the single `obscan_session_destroy -> OB_SCAN_SUCCESS` Report line:
```
[10/18 03:08:36.287556][info][...][Pipeline.cpp:69] Pipeline destroyed! @0x7CE12B0618
rotate pose for model.ply
-0.000000044, -1.000000000, 0.000000000, 0.000000000
1.000000000, -0.000000044, 0.000000000, 0.000000000
0.000000000, 0.000000000, 1.000000000, 0.000000000
0.000000000, 0.000000000, 0.000000000, 1.000000000

[worker] obscan_session_destroy                        -> OB_SCAN_SUCCESS (0)
[10/18 03:08:59.823659][info][...][PioneerDevice.cpp:59] Pioneer device destroyed! PID:3075, SN:CR0886100SN, ...
```
A 23.5s gap at 100ms poll intervals is consistent with roughly 235 polling iterations of `obscan_session_destroy` quietly returning `MULTI_CALLED` while the 16.6M-vertex/28.8M-triangle mesh serialization tail ran, before finally succeeding — and notably, unlike the failure pattern the dispatch specifically warned about, **no frame-drop warnings or GVCP noise are actively printing during this particular 23.5s gap** (that noise is present earlier, during the pause/stop window, but is quiet here). That absence of "clearly still running" noise during the gap argues against the specific failure mode described (a destroy succeeding immediately while visibly-active tail noise continues elsewhere in the log) — but it is not the same as directly observing refused destroy calls.

**Bottom line on (b): the code is structured to poll and should refuse with `MULTI_CALLED` during the tail per its own comments, and the ~23.5s quiet gap before the single logged success is consistent with that polling loop doing its job. But I did not and cannot directly observe a `MULTI_CALLED` status in this log, because the harness never prints intermediate poll results. Treat (b) as plausible-but-unconfirmed by direct log evidence, not as a verified pass.**

**(c) All three teardown call lines present, each eventually showing success? → PASS.**
```
[worker] obscan_scan_stop                              -> OB_SCAN_SUCCESS (0)
[worker] obscan_session_destroy                        -> OB_SCAN_SUCCESS (0)
[worker] obscan_context_destroy                        -> OB_SCAN_SUCCESS (0)
```
All three present, each `OB_SCAN_SUCCESS (0)`.

**(d) Exit code 0? → PASS.**
```
[worker] Frame protocol V1 counters: cloud_frames=3001 full_detail_chunks=833
[launcher] Worker exited normally with code 0.
EXIT_CODE=0
```

**(e) No watchdog fire? → PASS.**
`grep -n "child timed out after\|force-killed"` on the full raw log returns **no matches**. Neither string appears anywhere in the log.

## 4. #45 — protocol-version logging

Startup line, verbatim:
```
[worker] Frame protocol V1 (envelope: magic u32, version u16, type u16, seq u32, t_capture f64, t_server f64, payload_len u32 = 32 bytes)
```

Teardown counters summary, verbatim:
```
[worker] Frame protocol V1 counters: cloud_frames=3001 full_detail_chunks=833
```

## Main-session addendum: UI stayed responsive at scale (closes #39's acceptance)

Watched live via `ui/watch.sh` (a plain static file server, not an SDK action)
and the user physically moved the scanner for the session's duration.

- Live phase: connected throughout, render held steady at 60fps, data fps
  normal — consistent with the server-side series above.
- After pause and the full-detail chunk stream completed, the final HUD
  state read: `status: disconnected`, `render: 27.0 fps`, `points: 16659778`
  — an **exact match** to this record's final mesh vertex count
  (`num_vertices=16659778`). The page did not freeze or error while
  accumulating 833 full-detail chunks; render dipped from 60 to ~27fps while
  rendering the full 16.66M-point accumulated buffer (a one-time cost at
  disconnect, not a live-phase stall) but stayed interactive. Screenshot
  archived:
  [`2026-10-18-scan-server-scale-teardown-acceptance-final-hud.jpg`](./2026-10-18-scan-server-scale-teardown-acceptance-final-hud.jpg).
  The rendered point cloud is a coherent, recognizable 3D shape (not
  noise/garbage), further corroborating correct decode.

This closes #39's acceptance at scale: the client successfully requested
nothing extra (the stream is automatic in this build) but did accumulate and
render the full-detail path's entire output without failing, at 16.6M points
— well above the 2.2M case from the earlier V1 smoke test.

## Open questions

- The literal rule-6 `DYLD_INSERT_LIBRARIES=` banner line is absent from this launcher's stdout (see caveat above); confirm with whoever maintains the dispatch note whether `[vulkan_disable_interpose] loaded` is the intended/accepted substitute for `scan_server --live` runs generally, or whether the launcher should be emitting the literal line and currently isn't.
- #43(b) needs either (i) a harness change to log each poll attempt's status (out of scope for me to make — rule 4), or (ii) some other independent way to observe the SDK's internal `MULTI_CALLED` refusals, before it can be called a directly-confirmed pass rather than a timing-inference.
