# PLAN #175 — depth telemetry final re-run — FAIL

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_client/raptor_client  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 18:21:15 >= newest source main.cpp 18:21:07
- Interposer: n/a — links no vendor SDK (socket/STL only); vendor-artifact & interposer rules do not apply
- Run start: 2026-10-21 18:35:39
- Device serial: CR0886100SN
- HEAD SHA: 34d11e61aff07b7d206e7fb0641c686341fb236b

## Command

```
build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 8
```

## Result

- Exit code: `1`
- Verdict: **FAIL**
- Raw log: `2026-10-21-plan175-depth-telemetry-final.raw.log` (same directory) — **ABRIDGED**: GET_FRAME #6 through #62 (57 frames) and their associated SET calls are replaced by a single summary line (line 53 of the raw log); original output is not recoverable
- Raw log sha256: `f279af1496906630199d02d32b283504a0b843ce402623db10fee3411eaeab09`
- Interposer confirmation: n/a — links no vendor SDK

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | connect and start_stream succeed | **[PASS]** `[live] connected 127.0.0.1:10010 (depth, suffix 0-2-10, ...)` / `start_stream -> non-error` |
| 2 | data count nonzero | **[FAIL]** `done: get_frame=62 data=0 empty=62 error=0` |
| 3 | p50/p95/max latency values | **[PASS — timing captured]** `p50=187us p95=279us max=362us mean=195us` (wire round-trip only; first_frame=none) |
| 4 | Exit code 0 | **[FAIL]** exit code 1 |
| 5 | Compare with 2026-10-21-plan175-depth-addendum: same pattern? | **[PASS — consistent]** same as prior addendum: all frames empty, all SET prop calls return `vendor no ack (0 B)`, exit code 1 |

## Crash sweep

hw-postflight: no crash report for 'raptor_client' at/after 2026-10-21 18:35:39 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## On failure — observable + one lead

- Observable: `build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 8`, exit 1; `get_frame=62 data=0 empty=62`; all 62 GET_FRAME calls return `empty (kind=7)`; all 62 SET prop batches (props 3, 152, 153, 154, 155) return `vendor no ack (0 B)`; `first_frame=none`; same as prior addendum run.
- Lead: likely RPCServer depth stream (port 10010) is not delivering frame payloads — the depth channel itself or the vendor daemon's depth pipeline.

## Open questions

- Is the depth pipeline in the vendor daemon idle or inhibited when no Creality Scan app UI is active?
- Do props 152–155 require a prior successful GET_FRAME response before the vendor will ack them?
- Does running with the Creality Scan app open first change the frame delivery behaviour on port 10010?

## Board

- Row move: #175 `HW` → `HW` — depth still all-empty (data=0, empty=62), same as prior addendum; no regression, no fix yet
