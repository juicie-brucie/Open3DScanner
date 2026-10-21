# PLAN #168 — the `--capture` path retry (IR, 10013), post-fix — the hang is gone; the daemon did not answer this time

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (synced to origin/main @ `2174a66`)
- Target: build/tools/raptor_client/raptor_client (build-tree copy, fixed order 2: build/tools/<target>/<target>)
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon n/a — the target creates no OBScan context; freshness: binary 22:17:03 >= newest source `capture.cpp` 20:47:37
- Interposer: n/a — `raptor_client` links no vendor SDK; exempt from the interposer and vendor-artifact rules
- Run start: 2026-10-20 22:23:05

## Command

```
./build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 20 --frames 200 \
  --capture ~/captures/plan168-ir-retry --capture-seconds 20 --capture-bytes 500000000
```

## Result

- Exit code: `3`
- Verdict: **PASS for the fail-bounded fix (no hang, a complete manifest on an empty bundle); FAIL for the capture itself (0 records)**
- Raw log: `2026-10-20-plan168-ir-capture-retry.raw.log` (same directory)
- Interposer confirmation: n/a (exempt target)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | The connection to 10013 is established and the capture directory opens before any blocking call (row #168's fix: capture start no longer blocks) | **[PASS]** `[capture] recording every reply into .../plan168-ir-retry (bounds: 20 s, 500000000 B)` printed and flushed before the connect line |
| 2 | A silent daemon on a captured connection produces a **named, bounded** failure, not zero-output hang (the original 2-for-2 symptom) | **[PASS]** `[capture] FAILED: no complete reply on port 10013 for ob_rpc_start_stream0-2-12 within 5000 ms; the capture stopped answering` then `[live] TIMEOUT: ... no complete reply within 5000 ms - the connection was established and the daemon did not answer`; process exited in ~5 s (code 3), not killed |
| 3 | An incomplete bundle still writes a manifest naming the shortfall | **[PASS]** `manifest.json`: `"record_count": 0, "bytes_written": 0, "limits_hit": {"seconds": false, "bytes": false}`, `files: []`, plus `[capture] the bundle is incomplete: no complete reply on port 10013 for ob_rpc_start_stream0-2-12 within 5000 ms; the capture stopped answering` |
| 4 | The capture actually records IR frames (the row's unblocking goal for #132) | **[FAIL]** 0 records, 0 bytes — the very first call (`ob_rpc_start_stream0-2-12`) got no reply this run |

## Crash sweep

Not applicable — `raptor_client` links no vendor SDK and creates no device-process context; nothing here could produce a vendor crash report.

## On failure or block — observable + one lead

- Observable: `raptor_client --live 127.0.0.1 10013 20 --frames 200 --capture ~/captures/plan168-ir-retry --capture-seconds 20 --capture-bytes 500000000` → exit 3 in ~5 s, verbatim: `[live] TIMEOUT: 127.0.0.1:10013 (IR, suffix 0-2-12) ob_rpc_start_stream0-2-12: no complete reply within 5000 ms - the connection was established and the daemon did not answer`. Notably, this is the **same interface** (IR, port 10013) that answered 30/33 `get_frame` calls cleanly in the `#127` retry on 2026-10-20 earlier the same day — this run's `start_stream` itself got no reply at all.
- Lead: whether the daemon answers `ob_rpc_start_stream0-2-12` is session/state-dependent (e.g. a prior session left on the daemon side, or the daemon needs a settling period after the last client disconnected) is outside this row's wall (`tools/raptor_client/**`); #168's client-side fix is confirmed, the daemon-silence symptom is a new observation.

## Open questions

- Is the daemon's non-answer to `start_stream` here related to the immediately preceding #161 depth-interface run in this same session (daemon-side state left over from a timed-out depth connection), or independent?
- Does a cold daemon restart or a longer gap between runs change whether `start_stream` gets answered on the IR interface?

## Board

- Row move: #168 stays `HW` — the client-side fail-bounded deliverable (no hang, a named timeout, a manifest on an incomplete bundle) is confirmed, 1/1 clean on real hardware. The row's framing ("#132's IR/depth capture is blocked on this") is **not yet satisfied**: this retry captured 0 frames because the daemon did not answer `start_stream` at all. #168's own fix is done; the daemon-non-answer symptom is a new/separate observation for the dispatcher to route.
