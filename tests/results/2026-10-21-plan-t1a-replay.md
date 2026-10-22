# PLAN T1a — raptor_client replay (device-free) — FAIL

<!-- Second attempt / clean-RPCServer retry. Supersedes the first FAIL at this path
     (exit 141/SIGPIPE at frame 10, depth eof). -->

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: 442f8b075a2babdb6316d0f75b940bc336940906
- Target: build/tools/raptor_client/raptor_client  (explicit argument)
- Binary sha256: `14b9cda5e277ad5e102674aaae8b82b4bdc438d37c19f36dc0139b324f07a6a5`
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a; freshness: binary 18:21:15 >= newest source main.cpp 18:21:07
- Interposer: n/a — links no vendor SDK (socket/STL only); vendor-artifact & interposer rules do not apply
- Run start: 2026-10-21 21:39:34

### Run history at this slot

| Attempt | Time | Exit | Stopping event |
| --- | --- | --- | --- |
| 1 (concurrent T0) | 21:30:04 | 141 (SIGPIPE) | depth `ob_rpc_start_stream` → `Error(eof)` at frame 9/36 |
| 2 (clean-state retry) | 21:39:34 | 1 | IR `ob_set_ir_frame_light_type0-2-12` → Timeout at frame 1/36 |

## Command

```
build/tools/raptor_client/raptor_client \
  --replay docs/recon/findings/streaming-replay-payloads.hex \
  127.0.0.1 --ir-port 10013 \
  --dump-replies \
  --capture ~/captures/floor-replay
```

RPCServer port probe (Python connect_ex) before run:
- 10010: OPEN
- 10011: OPEN
- 10013: OPEN

## Result

- Exit code: `1`
- Verdict: **FAIL**
- Raw log: `2026-10-21-plan-t1a-replay.raw.log` (same directory; retry run)
- Raw log sha256: `ce826ebe1dd5157ecd2f17392b2e3d59f03ba3b1734060a802c273de209c9917`
- Interposer confirmation: not applicable

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Replay completes (exit 0) | **[FAIL]** exit 1; stopped at frame 1/36 |
| 2 | IR start_stream reaches `Bool(true)` | **[FAIL]** first IR frame timed out before start_stream was reached |
| 3 | Depth start_stream outcome observable | **[FAIL]** run aborted before any depth frame was sent; depth result not observable |
| 4 | Per-port band(N)/empty tally appears | **[FAIL]** no tally; 0 frames completed |
| 5 | Capture bundle non-empty | **[FAIL]** 0 records, 0 bytes written |

## Replay progress (observed)

Connections established for both interfaces (IR 0-2-12 → 127.0.0.1:10013; depth 0-2-10 → 127.0.0.1:10010).

First frame sent: `ob_set_ir_frame_light_type0-2-12` on IR port 10013.

```
[dump] reply method=ob_set_ir_frame_light_type0-2-12 kind=Timeout msgid=- envelope=Timeout result=Timeout lat=1999256us wire=42B
[replay] frame 1 (0-2-12): no reply within 2000 ms
```

Run terminated after frame 1. No depth frame was sent. Depth start_stream outcome is not observable in this run.

Capture bundle: `~/captures/floor-replay/` — manifest.json (732 B), replies/ (empty). 0 records, 0 bytes.

## Crash sweep

hw-postflight: no crash report for 'raptor_client' at/after 2026-10-21 21:39:34 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## On failure or block — observable + one lead

- Observable: command above; exit 1; IR port 10013 returned Timeout (lat=1999256 us) on `ob_set_ir_frame_light_type0-2-12` at frame 1/36; verbatim: `[replay] frame 1 (0-2-12): no reply within 2000 ms`; 0 frames captured; capture bundle reports "the capture stopped answering".
- Lead: likely in the RPCServer IR interface (port 10013) not acknowledging `ob_set_ir_frame_light_type` in a clean session state — distinct from the depth eof seen in attempt 1; the IR timeout is a new failure mode.

## Open questions

- Why does `ob_set_ir_frame_light_type0-2-12` time out at frame 1 in a clean-state session, when it succeeded for 8 frames in the concurrent-T0 attempt?
- Is `ob_set_ir_frame_light_type` the correct first frame to send on this firmware, or does the IR interface require a prior setup call not present in the replay file?
- Is depth start_stream reproducibly failing (as #175 suggests for depth-all-empty) independent of the IR timeout, or are the two failures linked?
- Does the RPCServer IR interface on port 10013 require the scanner device to be in a specific mode before accepting `ob_set_ir_frame_light_type`?

## Board

- Row move: T1a `HW` → `FAIL` (retry) — clean-state attempt reached 0/36 frames; IR port 10013 timed out on first frame (`ob_set_ir_frame_light_type`); depth outcome not observable; depth eof from attempt 1 not confirmed/refuted.
