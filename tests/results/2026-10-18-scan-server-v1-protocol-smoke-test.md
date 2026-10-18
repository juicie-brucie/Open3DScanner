# 2026-10-18 — scan_server V1 frame-protocol smoke test (PLAN.md row #42)

**Target:** `./build/tools/scan_server/scan_server --live 8787 "" 20`
**Worktree:** `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`
**Branch:** `hw/next`
**Purpose:** First runtime exercise of the V1 frame envelope
(`engine/include/openscanner/frame_protocol.h` + `ui/frame_protocol.js`,
landed in PR #104), plus a check of the #39/#95 on-demand full-detail path
(`full_mesh_start`/binary chunks/`full_mesh_done`, `tools/scan_server/main.cpp`
~lines 735-754). Deliberately short (session_seconds=20) to avoid
reproducing row #43's large-mesh teardown hang.

Raw log: `tests/results/2026-10-18-scan-server-v1-protocol-smoke-test.log`

## Pre-run checks

- `pwd` → `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`, branch `hw/next`. Confirmed before the first command.
- No other SDK-using process attached: `ps aux | grep -i scan_server` and `ps aux | grep -i obscan` both returned nothing before the run. (The concurrently running `ui/watch.sh` static file server on 8788 and its browser WS client, mentioned by the dispatching agent, are not SDK/device users and don't count against rule 7.)
- Build freshness: `build/tools/scan_server/scan_server` mtime `Oct 18 02:48:...116` (epoch 1792306116) vs `tools/scan_server/main.cpp` mtime epoch 1792306113 — binary is newer than source by 3s. Build looks fresh, not stale.

## Command and exit code

```
./build/tools/scan_server/scan_server --live 8787 "" 20
```
Started 2026-10-18T06:49:57Z, finished 2026-10-18T06:50:39Z (~42s wall clock: 20s session + teardown).

**Exit code: 0.**

## Banner check (rule 6)

The literal string `[launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib` specified by hard-rule 6 does **not** appear anywhere in the captured stdout/stderr (grep for `DYLD_INSERT_LIBRARIES` returns nothing). What the dispatching agent asked me to check instead — `[vulkan_disable_interpose] loaded` — **does** appear, as line 2 of the log, immediately after the launcher's startup message. This matches the pattern described for "all prior `scan_server --live` runs." I'm flagging the literal discrepancy rather than silently treating the substitute check as satisfying rule 6 at face value: this tool apparently never prints the DYLD_INSERT_LIBRARIES line to stdout/stderr (it's set via internal `setenv`, not the usual launcher wrapper banner), so `[vulkan_disable_interpose] loaded` is the only observable confirmation that the interposer is in the process. Given it's consistent with prior accepted runs per the dispatching agent, I proceeded, but note this for the record.

## Checks table

| Check | Result |
|---|---|
| `[vulkan_disable_interpose] loaded` banner present | [PASS] — log line 2 |
| SDK dylib loaded (not skip/exit 2) | [PASS] — `Loading OBScan SDK from: /Applications/CrealityScan.app/Contents/PlugIns/liborbbec_scan.dylib`, SDK version 3.5.3.260921 |
| Device discovered and session/scan created | [PASS] — `obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1`, `obscan_context_create_session -> OB_SCAN_SUCCESS`, `obscan_session_create_scan -> OB_SCAN_SUCCESS`, `obscan_scan_preview -> OB_SCAN_SUCCESS` |
| Scan started | [PASS] — `obscan_scan_start -> OB_SCAN_SUCCESS (0)`, `[worker] Streaming live scan frames on ws://0.0.0.0:8787` |
| Explicit V1/envelope protocol log line | **[NOT OBSERVED]** — grepped the raw log for `v1`, `protocol`, `envelope`, `frame_protocol` (case-insensitive): no matches. The server does not log the protocol version/envelope explicitly in this build. Per the dispatching agent's own fallback instruction, falling back to confirming frames are broadcast normally via the `[worker] tick N/M: color_cloud num_points=...` lines (next row) as indirect evidence that *some* framing is being used to carry the live data — this is NOT direct confirmation that the V1 envelope specifically is what's on the wire; it's inference from the UI-side decode (which the main session is capturing) that would close that gap. |
| Live per-tick series, growing, no stall (20s window) | [PASS] — `tick 0/200: num_points=0, clients=0` → `tick 50/200: num_points=470502, clients=2` → `tick 100/200: num_points=933603, clients=2` → `tick 150/200: num_points=1244924, clients=2`. Monotonically increasing, two clients connected throughout, no stall observed. (Tick 200/200 line was not reached/printed before pause — see note below.) |
| Pause → realtime mesh → final mesh preview broadcast | [PASS] — `obscan_scan_pause -> OB_SCAN_SUCCESS (0)`, then `obscan_scan_get_realtime_model_mesh -> OB_SCAN_SUCCESS (0), num_vertices=2158154, num_triangles=3181190`, then `[worker] Broadcast final reconstructed mesh preview (19983 of 2158154 points).` |
| Full-detail path (`full_mesh_start`/chunks/`full_mesh_done`) | [PASS] (indirect) — the code's own summary print appears verbatim: `[worker] Streamed full-detail mesh in 20000-point chunks.` The log does not print the literal tokens `full_mesh_start`/`full_mesh_done` (those are presumably the text-message type names sent over the socket, not separately logged to stdout at this verbosity) — this print is the only line confirming the chunked full-detail stream ran, and it ran successfully once between pause and stop. |
| Clean teardown (stop/destroy/exit) | [PASS] — all four required lines present in order: `obscan_scan_stop -> OB_SCAN_SUCCESS (0)` → (pipeline/device teardown, "rotate pose for model.ply" + 4x4 matrix dump) → `obscan_session_destroy -> OB_SCAN_SUCCESS (0)` → (Context destroyed) → `obscan_context_destroy -> OB_SCAN_SUCCESS (0)` → `[launcher] Worker exited normally with code 0.` |
| Exit code 0 | [PASS] |

## Notes / things worth flagging

- The resulting mesh (2,158,154 vertices / 3,181,190 triangles) is not small — it's well above the "small mesh" framing in the task description, though far below the 15.6M-vertex case that reproduced row #43's teardown hang. Despite that size, teardown completed cleanly and well inside the worker's own 140s internal timeout (total wall time ~42s). This is useful data for row #43: a ~2.2M-vertex mesh does **not** reproduce the hang in this run. It does not rule out the hang recurring at larger scale or under different conditions — this run simply didn't hit it.
- `tick 150/200` was the last tick line printed before the scan was paused (pause happened mid-session, consistent with the fixed 20s wall-clock session ending around there rather than completing all 200 ticks) — `tick 200/200` does not appear in the log, which is expected given the session cuts off by wall-clock time, not tick count.
- Standard SDK auto-exposure chatter (`中心亮度`, `Circle Points ...`, laser-current dumps) and benign GVCP broadcast noise (`sendto failed with error:49`, `bind 0.0.0.0:...`) are present throughout, consistent with prior runs; not treated as failures.
- `vkCreateInstance failed -3` after context destroy, with `[vulkan_disable_interpose] blocked vkCreateInstance` immediately before it, appears right at the very end of teardown (after `obscan_context_destroy` line even precedes it in the log — actually it appears between context-destroy-adjacent GVCP noise and the final `obscan_context_destroy` success line). This is the interposer deliberately blocking a late Vulkan instance-creation attempt during teardown, consistent with the interposer's purpose; the subsequent `obscan_context_destroy -> OB_SCAN_SUCCESS (0)` and clean `Worker exited normally with code 0` show it did not disrupt teardown.
- No crash report: checked `~/Library/Logs/DiagnosticReports/` for the run's time window (around 2026-10-18 06:49:57Z–06:50:39Z local); `ls` filtered for `scan_server` returned nothing, consistent with the clean exit 0 (not expected to need one, and none found).
- No V1-envelope-specific log line exists in this build to directly confirm the wire format from the server side alone (see table row above) — the UI-side decode (main session, watching `ui/frame_protocol.js` parse the stream) is needed to close that loop and is explicitly out of scope for this report.

## Main-session addendum: UI-side decode closes the V1-confirmation gap

The server log alone can't prove V1 framing is on the wire (see row above and
open question 2) — but the client side can, by actually succeeding at
decoding it. While this run was in progress, the main session watched the
live HUD (`ui/watch.sh`, a plain static file server — not an SDK action) and
confirmed:

- Live phase: connected, `status: connected`, `scan: scanning (live)`,
  `render` steady at 60fps, points climbing normally through the capped
  live-broadcast budget — ordinary decode of the v1 binary frame type via
  `ui/frame_protocol.js`.
- Full-detail phase: after pause, the page received and accumulated the
  `full_mesh_start`/chunk/`full_mesh_done` text+binary sequence and rendered
  a coherent, recognizable 3D shape (not noise/garbage) at the end — the
  final-state screenshot shows `points: 2158154`, exactly matching this
  record's `num_vertices=2158154` from `obscan_scan_get_realtime_model_mesh`.
  Archived at
  [`2026-10-18-scan-server-v1-protocol-smoke-test-final-hud.jpg`](./2026-10-18-scan-server-v1-protocol-smoke-test-final-hud.jpg).

A client that successfully parses an envelope it doesn't understand into a
garbled or zero-point render would not produce an exact, legible count match
like this — so the matching count plus a coherent render is reasonably
strong end-to-end evidence that `ui/frame_protocol.js` decoded the same V1
envelope `frame_protocol.h`/`main.cpp` encode, for both the live-tick and
full-detail-chunk message shapes. This doesn't answer open question 2 (there
is still no server-side log naming "v1" explicitly) but it does answer the
underlying concern: the protocol does work end-to-end, exercised live, for
the first time.

## Open questions

1. Is the absence of the literal `[launcher] DYLD_INSERT_LIBRARIES=...` banner line in stdout/stderr expected/by-design for `scan_server --live` (since it sets the env var itself via internal `setenv` rather than through the usual launcher wrapper), or should that line be emitted somewhere this capture missed?
2. Does the server have any existing log line that names the V1 envelope/protocol version explicitly, or is confirming it depends entirely on the client-side decode (`ui/frame_protocol.js`) succeeding against the byte stream? If the intent is for row #42 to be verifiable from server logs alone, that seems like a logging gap.
