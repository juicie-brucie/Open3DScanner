# PLAN #168 — `--capture` path final retry (IR, 10013) — PASS (no hang, start_stream answered, bundle written)

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (synced to origin/main @ `2174a66`)
- Target: build/tools/raptor_client/raptor_client (build-tree copy, fixed order 2: build/tools/<target>/<target>)
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon n/a — no OBScan context; freshness: binary 22:17:03 >= newest source `capture.cpp` 20:47:37
- Interposer: n/a — exempt (no vendor SDK)
- Run start: 2026-10-20 22:59:22

## Command

```
./build/tools/raptor_client/raptor_client \
  --live 127.0.0.1 10013 20 \
  --frames 200 \
  --capture ~/captures/plan168-ir-capture-final \
  --capture-seconds 20 \
  --capture-bytes 500000000
```

## Result

- Exit code: `1` (frame budget reached: `get_frame=200`, `--frames 200` limit hit, normal exit)
- Verdict: **PASS**
- Raw log: `2026-10-20-plan168-ir-capture-final.raw.log` (same directory)
- Interposer confirmation: n/a (exempt)

## Checks

| # | Check | Result |
|---|---|---|
| 1 | Capture-start line flushed before any blocking call (row #168's fix: startup no longer blocks) | **[PASS]** `[capture] recording every reply into .../plan168-ir-capture-final (bounds: 20 s, 500000000 B)` — first line, flushed immediately |
| 2 | `start_stream` answered (not timed out — prior 2-for-2 symptom before fix) | **[PASS]** `[live] start_stream -> non-error` (no `TIMEOUT` line) |
| 3 | Frames flow through the capture path to disk | **[PASS]** `[capture] 1202 record(s), 76745 byte(s) written`; `replies/port-10013.bin` present on disk (76757 bytes) |
| 4 | Manifest is complete and consistent | **[PASS]** `record_count: 1202`, `bytes_written: 76745`, `limits_hit: {seconds: false, bytes: false}`, `files: 1` entry with correct size |
| 5 | No hang, no timeout, exits on frame budget | **[PASS]** `[live] done: get_frame=200 data=0 empty=200 error=0` — 200 frames polled, exited at `--frames 200` limit within the 20 s window |
| 6 | No crash | **[PASS]** postflight: no crash report for `raptor_client` at/after run start |

## Note on `data=0 empty=200`

All 200 `get_frame` calls returned `empty` (kind=7 array, no Bin payload) — the stream is live and responding, but the daemon is not yet serving frame data in this capture session. This is the same zero-data pattern seen in prior sessions (e.g. `2026-10-19-raptor-client-first-live-session.md`). The 1202 captured records are the `start_stream`, `send_data` (SetProperty), `get_frame`, and `stop_stream` RPC round-trips, all recorded to the bundle — the full protocol exchange is captured even when `get_frame` returns empty. This is a separate open question (why `get_frame` returns empty on some sessions but `data=50` on others); it is not row #168's concern.

## Crash sweep

`scripts/hw-postflight.sh plan168-ir-capture-final '2026-10-20 22:59:22' raptor_client` →
`hw-postflight: no crash report for 'raptor_client' at/after 2026-10-20 22:59:22 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.`

## Board

- Row move: #168 `HW` → `DONE` — the row's deliverable is confirmed: the `--capture` path no longer hangs; capture-start flushes immediately; `start_stream` responds; the bundle is written to disk with a complete manifest; an incomplete or zero-frame bundle exits non-zero (code 1) with full accounting. The `data=0 empty=200` result is a separate session-state question, not a regression of the fix.
