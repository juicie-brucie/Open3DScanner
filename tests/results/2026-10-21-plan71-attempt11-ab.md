# PLAN #71 — attempt 11 A/B plane-removal toggle — FAIL

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server  (build-tree copy, fixed order 2: build/tools/<target>/<target>)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (12:53:07) — refreshed only on an app build (#107)
- Preflight: pwd OK; no conflicting process; daemon: RPCServer(53211,) running (process proxy: a vendor RPC host process exists); freshness: binary 12:53:07 >= newest source main.cpp 12:52:55
- Interposer: expect line 2: `[vulkan_disable_interpose] loaded` — **present**, line 2
- Run start: 2026-10-21 19:22:53

## Command

```
OPENSCANNER_PLANE_REMOVE=0 ./build/tools/scan_server/scan_server --live 8787
# ws_toggle.py started concurrently: connect to ws://127.0.0.1:8787, wait 20s, send
#   {"cmd":"set_plane_removal","auto":true}
# wait 20s, send
#   {"cmd":"set_plane_removal","auto":false}
# wait 10s, exit
```

## Result

- Exit code: SIGKILL (killed after ws_toggle completed; normal forced teardown)
- Verdict: **FAIL** — the toggle was never delivered; no `plane_removal:` log lines in the 9033-line log
- Raw log: `2026-10-21-plan71-attempt11-ab.raw.log` (same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` — present, line 2

## Sequence

- 19:22:53 — scan_server started
- 19:22:55 — device enumerated: `list: (1)`, `count=1`
- 19:22:59 — `obscan_scan_start -> OB_SCAN_SUCCESS`; WebSocket server up on ws://0.0.0.0:8787
- 19:23:01 — ws_toggle.py connected (`clients=0.4` in window 1; the 0.4 fraction indicates connection held for ~2s of the 5s window)
- 19:23:05 — window 2: `clients=0.1`; `bcast_send n=11 mean=18284.73us max=200991 us`; `bcast_drops=1 timeouts=1` — server's outbound send timed out; connection dropped
- 19:23:10 onward — `clients=0.0` in all 14 subsequent windows; no plane_removal lines
- 19:24:25 — SIGKILL; context destroyed cleanly (`obscan_context_destroy -> OB_SCAN_SUCCESS`)

## Points-per-tick series (all unaffected by any toggle)

| Window | points/tick |
|---|---|
| 1 | 891.9 |
| 2 | 2187.3 |
| 3 | 2571.8 |
| 4 | 3515.8 |
| 5 | 3706.6 |
| 6 | 3864.1 |
| 7 | 11755.9 |
| 8 | 12714.1 |
| 9 | 12791.7 |
| 10 | 12927.1 |
| 11 | 12868.1 |
| 12 | 12876.8 |
| 13 | 12968.0 |
| 14 | 13082.6 |
| 15 | 13067.3 |
| final | 13182.4 |

Summary: 782 ticks, mean 9062.6 points/tick, 2342/2342 tracking SUCCESS.

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Device enumerated, scan_start reached | **[PASS]** `list: (1)`, `count=1`, `obscan_scan_start -> OB_SCAN_SUCCESS` at ~6s |
| 2 | Interposer line 2 present | **[PASS]** `[vulkan_disable_interpose] loaded` |
| 3 | OPENSCANNER_PLANE_REMOVE=0 startup (no auto plane removal at boot) | **[PASS]** no startup `plane_removal: ... [auto 0->1 startup]` line |
| 4 | WebSocket toggle command delivered | **[FAIL]** ws_toggle.py connection dropped by server (bcast_drop=1, bcast_timeout=1, clients=0.0 from window 3 onward); no `plane_removal:` lines in log |
| 5 | `plane_removal:` log at toggle enable | **[FAIL — not observed]** no log line; command never received by scan_server |
| 6 | `plane_removal:` log at toggle disable | **[FAIL — not observed]** command never received |
| 7 | Teardown clean | **[PASS]** `obscan_context_destroy -> OB_SCAN_SUCCESS`; 2342 tracking SUCCESS |

## Crash sweep

`scripts/hw-postflight.sh plan71-attempt11-ab '2026-10-21 19:22:53' scan_server` →
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-21 19:22:53 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

## On failure or block — observable + one lead

- Observable: `ws_toggle.py` connected (clients=0.4 in window 1), but server dropped the connection in window 2 with `bcast_send max=200991us`, `bcast_drops=1`, `bcast_timeouts=1`; from window 3 onward `clients=0.0`; no `plane_removal:` line appears anywhere in the 9033-line log; the toggle was never sent.
- Lead: ws_toggle.py holds the WebSocket connection idle (no recv/drain loop) while waiting 20s before sending — the server's outbound broadcast fills the socket buffer, its send times out, and the connection is closed; the fix is to drain incoming frames while waiting (add a non-blocking recv loop in the wait period, or send the toggle immediately after connect with only a brief stabilisation pause).

## Open questions

- Does the WsBroadcastServer have a configurable idle-client timeout, or is the disconnect caused solely by the outbound send timeout (`bcast_send max=200ms`)?
- Would a WebSocket client that drains incoming frames (continuously reads and discards status messages) during the wait period keep the connection alive long enough to deliver the toggle?
- Is the right design for a scripted A/B test to send the toggle immediately after `obscan_scan_start` is confirmed in the log (after a shorter wait), rather than holding an idle connection for 20s?

## Board

- Row move: #71 stays `HW` — scan_server ran cleanly (782 ticks, 2342/2342 SUCCESS, clean teardown), but the toggle was not delivered due to the ws_toggle client dropping its WebSocket connection before the send; the owed observable (a measurable effect at the toggle boundary) remains unconfirmed.
