# 2026-10-18 — scan_server `--live` backpressure (#41) + follow-pose (#48) smoke, with #32/#43/#46 re-checks

## Target / command

```
cd /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
./build/tools/scan_server/scan_server --live 8787 "" 300
```

Build freshness check before running: `build/tools/scan_server/scan_server` mtime
`Oct 18 04:29:47 2026`, `tools/scan_server/main.cpp` mtime `Oct 18 04:29:44 2026`
— binary is newer than source, not rebuilt by me.

- Run start: ~04:36:34 EDT 2026 (pre-run check), process launched ~04:36:38–04:36:39
- Run end: process exited normally, log's final line timestamped by launcher after
  `[worker] Frame protocol V1 counters: cloud_frames=3001 full_detail_chunks=721`
- **Exit code: 0**
- Internal worker timeout confirmed in source: `tools/scan_server/main.cpp:1060`,
  `timeout_seconds = session_seconds + 300` → 600s for this 300s session (current
  formula, not the old +120).
- No concurrent SDK-using process was running before this test (`ps aux` checked
  for `scan_server`, `device_message_listener_test`,
  `data_process_message_listener_test`, `live_capture_test`, `smoke_test` — no
  matches).
- Raw log: `tests/results/2026-10-18-scan-server-backpressure-follow-smoke.log`
  (copied verbatim from the command's captured stdout/stderr).
- No crash report found in `~/Library/Logs/DiagnosticReports/` for `scan_server`
  (checked after the run; consistent with the clean exit 0 — not expected to
  need one for this run, but checked per rule 11 anyway).

## Verdict: PASS (server-side evidence only; #41 drop event itself not directly observable, as predicted by source inspection)

## Checks

| # | Check | Result |
|---|---|---|
| 1 | Banner / launcher-correctness (rule 6) | **[FAIL — literal line absent]** `grep DYLD_INSERT_LIBRARIES` found nothing in the log. Only the older substitute `[vulkan_disable_interpose] loaded` appears (log line 2, right after `[launcher] Running live scan server...`). So PR #133's literal `[launcher] DYLD_INSERT_LIBRARIES=...` banner line is **not present in this build's output** — I'm reporting the substitute as the basis for trusting this was the interposed run, per rule 6's allowed fallback, but the literal line itself did not appear. |
| 2 | #41 tick cadence steady throughout (incl. during main session's JS-thread stalls) | **[PASS, as far as observed]** 60 `clients=` tick lines, every 50 ticks from tick 0 to tick 2950. `clients=1` on every single one from tick 50 onward (tick 0 was `clients=0`, pre-connect). No stall, no gap, no error between tick lines; point counts increased or plateaued smoothly tick-over-tick the entire run (see `#32` table below). I did not have the main session's precise stall-window timestamps to cross-reference, so I can only report "no stall observed in the full log," not "confirmed no stall during the exact stall window." |
| 3 | #41 client-count drop/prune signal | **[NOT OBSERVED]** `clients=` never dropped to 0 after the initial connect at tick 50; it stayed `1` for the rest of the run. No evidence the stalled-client's socket was pruned/closed — but also no evidence the server blocked on it. This is consistent with (not proof of) the 100ms `SO_SNDTIMEO` working: a slow client's send times out and that frame is dropped for them, without disconnecting them and without stalling the broadcast loop. |
| 4 | #41 explicit drop/timeout log line | **[ABSENT, as predicted]** No stdout line anywhere in the log mentions a timed-out or dropped send (grepped for the known lack of such a line per source inspection of `ws_broadcast_server.cpp`'s `SendAll`/accept loop — confirmed there's no such print path). This smoke test cannot directly confirm #41's drop mechanism fired; it can only confirm the *absence of a stall*, which is the externally observable half of what #41 is supposed to guarantee. |
| 5 | #48 pose-frame log evidence | **[ABSENT, as predicted]** `grep -ni pose` across the full log returns 4 hits, all unrelated to the per-tick pose broadcast: line 2 (`[vulkan_disable_interpose] loaded`, a false match on the substring "interpose"), line 11791 (`rotate pose for model.ply`, the SDK's own `.ply`-export rotation-matrix print during the final mesh save), and lines 11830-11831 (`[vulkan_disable_interpose] intercepted...`/`blocked vkCreateInstance`, same false substring match at teardown). There is **no per-tick stdout evidence of pose frames being sent** — consistent with source inspection (`tools/scan_server/main.cpp` ~786-790 calls `encode_pose` and `BroadcastBinary` per tick but prints nothing). Server-side, I can only say: the code path that calls `obscan_scan_get_registration_pose` and broadcasts its result ran every tick for the whole session (it's unconditional in the tick loop, gated only by `hardware_paused`, which was `0`/false for the whole run per every tick line's `paused=0`), but I have no direct per-frame confirmation the SDK call itself returned `OB_SCAN_SUCCESS` each time (no per-call status print) or that bytes reached a socket. Client-side confirmation (follow mode visibly tracking the scanner) is outside what this server-side evidence can establish; I'm not able to confirm or deny that from here. |
| 6 | #32 point-count ramp ≥1,000,000 | **[PASS]** Tick 250 already at `num_points=979,310`; tick 300 reached `1,044,043` (max seen so far `1,170,885`); sustained well above 1M for the rest of the run, ending near tick 2950 at `num_points=1,367,230` (max seen so far `1,367,230`). Final `obscan_scan_get_realtime_model_mesh` call reports `num_vertices=14,419,410, num_triangles=23,066,583` (mesh, not cloud — much larger, as expected for the reconstructed surface vs. the live point cloud). |
| 7 | #43/#46 teardown — immediate `obscan_scan_stop` return | **[PASS]** `[worker] obscan_scan_stop  -> OB_SCAN_SUCCESS (0)` prints immediately after the final mesh-preview broadcast lines, with no intervening poll/wait logged for that call itself (the poll loop is only for the two destroy calls that follow). |
| 8 | #43/#46 teardown — destroy-poll pattern, now with per-poll logging (closing #46's gap) | **[PASS — #46's gap is closed]** Per-poll log lines **are now present**, exactly matching the `refusals == 1 \|\| refusals % 10 == 0` logging condition confirmed in source (`tools/scan_server/main.cpp:945-948`, `968-971`). Quoted directly from the raw log for `obscan_session_destroy`: <br>`[worker] obscan_session_destroy poll 1: refused (OB_SCAN_ERROR_MULTI_CALLED)` <br>`[worker] obscan_session_destroy poll 10: refused (OB_SCAN_ERROR_MULTI_CALLED)` <br>`[worker] obscan_session_destroy poll 20: refused (OB_SCAN_ERROR_MULTI_CALLED)` <br>... (continuing every 10 through) ... <br>`[worker] obscan_session_destroy poll 160: refused (OB_SCAN_ERROR_MULTI_CALLED)` <br>`[worker] obscan_session_destroy: 168 poll(s), 167 refusal(s), 17.2s, final OB_SCAN_SUCCESS` <br>For `obscan_context_destroy`, only 1 poll with 0 refusals was needed this run (`[worker] obscan_context_destroy: 1 poll(s), 0 refusal(s), 1.7s, final OB_SCAN_SUCCESS`), so no per-poll "refused" lines were emitted for it (none were due, by the logging condition — it succeeded on the first try). |
| 9 | #43/#46 teardown — all three teardown lines + final statuses | **[PASS]** All three present with `OB_SCAN_SUCCESS` final status: `obscan_scan_stop -> OB_SCAN_SUCCESS (0)`; `obscan_session_destroy: 168 poll(s), 167 refusal(s), 17.2s, final OB_SCAN_SUCCESS`; `obscan_context_destroy: 1 poll(s), 0 refusal(s), 1.7s, final OB_SCAN_SUCCESS`. |
| 10 | #43/#46 teardown — exit 0 | **[PASS]** `[launcher] Worker exited normally with code 0.` and the shell-reported exit code was `0`. |
| 11 | #43/#46 teardown — no watchdog fire | **[PASS]** `grep -n "child timed out after\|force-killed"` over the full log returned no matches. |

## Main-session addendum: the client-side half could not run at all — row #54 filed

Before and during this run, the main session attempted the client-side half
of this smoke test (toggling follow mode, stalling the browser tab's JS
thread to exercise #41's drop-gate) via `ui/watch.sh` + a browser tab. The
page never got past its static `connecting...` placeholder. The browser
console showed:

```
Uncaught SyntaxError: Identifier 'frame' has already been declared
```

reproducible on reload. `ui/app.js`'s `ws.onmessage` handler declares
`const frame = FrameProtocol.decodeCloudSnapshot(buf);` twice in the same
function scope (lines 245 and 269) — a `SyntaxError` that aborts the whole
script's parse, so **nothing in the page runs**: no WS connect, no draw
loop, no HUD, no follow-mode toggle, no drop-gate to stall. This is a
pre-existing bug at the `origin/main` tip this run used (`4118b46`), not
something this run or its environment caused — filed as **row #54**
(`TODO`), and it fully explains why this record's #41/#48 client-side rows
are "not observed": there was no working client to observe them with. The
server-side evidence above stands on its own regardless.

## Notes / caveats

- The literal `[launcher] DYLD_INSERT_LIBRARIES=...` banner line specified by rule 6 did **not** appear in this run's output, even though PR #133 is reportedly meant to add it. Only the older `[vulkan_disable_interpose] loaded` substitute line appeared (first line after the one `[launcher] Running live scan server...` line). I'm treating the substitute as sufficient confirmation this was the interposed binary (consistent with every other piece of evidence — `vkCreateInstance` blocked at teardown, worker SDK logs present, etc.), but this is worth flagging: either PR #133 hasn't actually landed in this build despite the task description's expectation, or the literal line is only printed under some condition not hit here. I did not modify anything to chase this down (out of scope / `main.cpp` is test code I'm not to touch).
- I cannot distinguish "the SO_SNDTIMEO-based per-client drop logic fired and worked" from "it was simply never exercised" purely from this server log — there's no instrumentation for it. The only server-side signal available is the absence of a stall, which I observed. The main session's client-side observations (whether the browser tab felt backpressure / dropped frames correctly while its own JS thread was blocked) are the authoritative evidence for #41's actual drop behavior; I have no access to that from here.
- Similarly for #48: I can confirm the `encode_pose`/`BroadcastBinary` call site in the tick loop structurally ran every un-paused tick (all ticks, `paused=0` throughout), but I have no byte-level or per-call confirmation from this log that the pose frames were well-formed or received. The main session's visual confirmation of follow-mode tracking is the authority here.
- Tick-to-wall-clock cadence: tick lines are logged every 50 ticks (every ~5s nominal at 100ms/tick), interleaved with SDK-internal timestamped lines. Cross-referencing nearby SDK timestamps: tick 0 ≈ 04:36:43, tick 500 ≈ 04:37:41 (~58s for 500 ticks, vs. nominal 50s — some slack, not a stall), tick 1500 ≈ 04:39:41, tick 2500 ≈ 04:41:38, teardown's `obscan_scan_stop` at 04:43:14. Total worker run was closer to ~6.5 min wall-clock for a nominal 300s/3000-tick budget, i.e. actual tick period ran a bit slower than the nominal 100ms under real hardware load — expected behavior, not a cadence failure, and well inside the 600s internal watchdog budget.
