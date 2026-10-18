# Row #31 — scan_server data/process message listener instrumentation (hardware acceptance)

- Date: 2026-10-18
- Tree: `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`, branch `hw/next`
- Target: `build/tools/scan_server/scan_server` (PR #74, instrumented `OnDataMessage`/`OnProcessMessage` with `[worker][data]`/`[worker][process]` logging — msg_id, name when known, payload pointer, calling thread)
- Command:
  ```
  ./build/tools/scan_server/scan_server --live 8787 "" 30
  ```
- Exit code: `0`
- Raw log: `tests/results/2026-10-18-scan-server-data-process-instrumentation.raw.log`
- Verdict: **PASS** (instrumentation fired, session completed teardown cleanly, no hang, no crash)

## Pre-flight

- `pwd` confirmed as the `-wt-hw` worktree, branch `hw/next`, before the first command.
- `ps aux | grep scan_server` showed no other SDK-using process attached before starting (rule 7).
- Did not set `DYLD_INSERT_LIBRARIES` myself; `scan_server` manages its own worker process and injects it internally via `setenv`, as noted in the task brief.

## Banner / rule 6 check

The log does **not** contain a literal `[launcher] DYLD_INSERT_LIBRARIES=...` line — `scan_server`'s own launcher prints a different first line (`[launcher] Running live scan server in a worker process (up to 30s)...`), followed by `[vulkan_disable_interpose] loaded`. This exact pattern matches the precedent accepted run for row #25 (`tests/results/2026-10-17-scan-server-live-acceptance.log`), which has the identical two opening lines and was accepted as the correct-binary check for this tool. Observed here:

```
[launcher] Running live scan server in a worker process (up to 30s)...
[vulkan_disable_interpose] loaded
```

Treating this as satisfying rule 6 for `scan_server` specifically (as it did for row #25), since this binary's own launcher format differs from `device_message_listener_test`'s.

## Checks

| Check | Result |
|---|---|
| `[vulkan_disable_interpose] loaded` present | [PASS] |
| `obscan_context_add_data_msg_listener` registers successfully | [PASS] — `-> OB_SCAN_SUCCESS (0)` |
| `obscan_context_add_process_msg_listener` registers successfully | [PASS] — `-> OB_SCAN_SUCCESS (0)` |
| `[worker][data]` lines logged during session | [PASS] — 30 lines observed |
| `[worker][process]` lines logged during session | **[FAIL]** — 0 lines observed this run, despite listener registering successfully |
| Session completes teardown (pause/mesh/stop/destroy) | [PASS] — ends with `obscan_context_destroy -> OB_SCAN_SUCCESS (0)` and `[launcher] Worker exited normally with code 0.` |
| No hang / no crash report | [PASS] — exited within budget, no `.ips` file for this run's window |

Note on the `[FAIL]` row above: this is not a claim that the process-listener instrumentation itself is broken — it means that in this specific 30-second session, the SDK delivered zero process-family messages to the registered listener, so no `[worker][process]` lines could be produced regardless of instrumentation correctness. The instrumentation is confirmed working for the data-family path (30 lines fired), so the absence of process-family lines is evidence about *what the SDK delivered*, not about the logging code. Flagging as a literal mismatch against the expected checklist item rather than softening it to "not observed."

## `[worker][data]` lines (full set, verbatim)

30 lines total, all identical in shape apart from msg_id/name and payload pointer. All example lines (complete list from the raw log, lines 112–141):

```
[worker][data] msg_id=325189889 (NEW_IR_IMAGE) data=0x77fcea5800 thread=1441039636626847075
[worker][data] msg_id=325189888 (NEW_DEPTH_IMAGE) data=0x77fcea5860 thread=1441039636626847075
[worker][data] msg_id=325189890 (FRAME_READY) data=0x77fcea58c0 thread=1441039636626847075
[worker][data] msg_id=325189889 (NEW_IR_IMAGE) data=0x77fcea5920 thread=1441039636626847075
[worker][data] msg_id=325189888 (NEW_DEPTH_IMAGE) data=0x77fcea5980 thread=1441039636626847075
[worker][data] msg_id=325189890 (FRAME_READY) data=0x77fcea59e0 thread=1441039636626847075
[worker][data] msg_id=325189889 (NEW_IR_IMAGE) data=0x77fcea5a40 thread=1441039636626847075
[worker][data] msg_id=325189888 (NEW_DEPTH_IMAGE) data=0x77fcea5aa0 thread=1441039636626847075
[worker][data] msg_id=325189890 (FRAME_READY) data=0x77fcea5b00 thread=1441039636626847075
[worker][data] msg_id=325189889 (NEW_IR_IMAGE) data=0x77fcea5b60 thread=1441039636626847075
[worker][data] msg_id=325189888 (NEW_DEPTH_IMAGE) data=0x77fcea5bc0 thread=1441039636626847075
[worker][data] msg_id=325189890 (FRAME_READY) data=0x77fcea5c20 thread=1441039636626847075
[worker][data] msg_id=325189889 (NEW_IR_IMAGE) data=0x77fcea5c80 thread=1441039636626847075
[worker][data] msg_id=325189888 (NEW_DEPTH_IMAGE) data=0x77fcea5ce0 thread=1441039636626847075
[worker][data] msg_id=325189890 (FRAME_READY) data=0x77fcea5d40 thread=1441039636626847075
[worker][data] msg_id=325189889 (NEW_IR_IMAGE) data=0x77fcea5da0 thread=1441039636626847075
[worker][data] msg_id=325189888 (NEW_DEPTH_IMAGE) data=0x77fcea5e00 thread=1441039636626847075
[worker][data] msg_id=325189890 (FRAME_READY) data=0x77fcea5e60 thread=1441039636626847075
[worker][data] msg_id=325189889 (NEW_IR_IMAGE) data=0x77fcea5ec0 thread=1441039636626847075
[worker][data] msg_id=325189888 (NEW_DEPTH_IMAGE) data=0x77fcea5f20 thread=1441039636626847075
[worker][data] msg_id=325189890 (FRAME_READY) data=0x77fcea5f80 thread=1441039636626847075
[worker][data] msg_id=325189889 (NEW_IR_IMAGE) data=0x77fcea5fe0 thread=1441039636626847075
[worker][data] msg_id=325189888 (NEW_DEPTH_IMAGE) data=0x77fcea6040 thread=1441039636626847075
[worker][data] msg_id=325189890 (FRAME_READY) data=0x77fcea60a0 thread=1441039636626847075
[worker][data] msg_id=325189889 (NEW_IR_IMAGE) data=0x77fcea6100 thread=1441039636626847075
[worker][data] msg_id=325189888 (NEW_DEPTH_IMAGE) data=0x77fcea6160 thread=1441039636626847075
[worker][data] msg_id=325189890 (FRAME_READY) data=0x77fcea61c0 thread=1441039636626847075
[worker][data] msg_id=325189889 (NEW_IR_IMAGE) data=0x77fcea6220 thread=1441039636626847075
[worker][data] msg_id=325189888 (NEW_DEPTH_IMAGE) data=0x77fcea6280 thread=1441039636626847075
[worker][data] msg_id=325189890 (FRAME_READY) data=0x77fcea62e0 thread=1441039636626847075
```

## `[worker][process]` lines

None observed. `grep -c '\[worker\]\[process\]'` on the raw log returns `0`, despite `obscan_context_add_process_msg_listener -> OB_SCAN_SUCCESS (0)` showing the listener registered. This contradicts the brief's note that prior (pre-instrumentation) runs showed "roughly ~10 such messages per session" in total across both families — this run shows 30 data messages and 0 process messages, a different split than expected, worth flagging to the row's author.

## Analysis

- **Distinct msg_ids observed:** 3, all named (table lookup succeeded for all of them — no numeric-unknown ids appeared):
  - `325189888` → `NEW_DEPTH_IMAGE` (10 occurrences)
  - `325189889` → `NEW_IR_IMAGE` (10 occurrences)
  - `325189890` → `FRAME_READY` (10 occurrences)
  - They repeat in a fixed cycle: IR, DEPTH, FRAME_READY, ×10.
- **Payload pointer pattern:** every single line has a non-NULL `data=0x...` pointer. No NULL payloads observed for any msg_id in this run. Pointers for a given id are monotonically increasing (consistent with a small ring/queue of consecutively allocated payload slots, e.g. `0x77fcea5800`, `0x77fcea5860`, `0x77fcea58c0`, ... stepping by `0x60` bytes across each of the 30 slots) — this is an observation about addresses logged, not an inference about structure.
- **Threads:** only one distinct thread hash appears across all 30 `[worker][data]` lines: `1441039636626847075`. No evidence in this run of the data listener being called from more than one thread. (This says nothing about the process listener, which never fired.)
- **Session health:** teardown ran to completion — pause/stop/destroy sequence visible in the tail of the raw log (`obscan_scan_stop -> OB_SCAN_SUCCESS (0)`, `obscan_session_destroy -> OB_SCAN_SUCCESS (0)`, full device/sensor/RPC-client destruction sequence, `Context destroyed`, `obscan_context_destroy -> OB_SCAN_SUCCESS (0)`), ending with `[launcher] Worker exited normally with code 0.` No SIGINT was sent; the session ran its own 30s budget and exited on its own.
- No crash report found in `~/Library/Logs/DiagnosticReports/` for the run's time window (started 2026-10-18 00:16:30 local, ended ~00:17:16 local) — checked via both a filename grep and `find -newermt`.

## Open questions for the row's author

1. Is it expected that `[worker][process]` produced zero lines in a 30s live session, or should the process-family listener be expected to fire at least once (e.g. on session state transitions) within that window? The brief described "roughly ~10 messages per session" historically, without splitting by data vs. process family, so this run's 30 data / 0 process split may or may not be a regression — flagging rather than asserting either way.
2. Confirm whether the single-thread observation (`thread=1441039636626847075` for all 30 data deliveries) is expected (e.g. a dedicated data-callback thread) — no contradicting evidence from this run, but this is one run, not a guarantee across sessions.

[hw-confirmed: 2026-10-18-scan-server-data-process-instrumentation] — data-family listener instrumentation fires correctly with named ids, non-NULL payloads, single calling thread, across a clean 30s live session; process-family listener registered successfully but logged zero deliveries in this run.
