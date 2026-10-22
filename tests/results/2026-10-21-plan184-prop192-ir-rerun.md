# PLAN #184 — raptor_client IR re-run (prop=192 ACCEPT/REJECT check) — BLOCKED

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_client/raptor_client  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 18:21:15 >= newest source main.cpp 18:21:07
- Interposer: n/a — links no vendor SDK (socket/STL only); vendor-artifact & interposer rules do not apply
- Run start: 2026-10-21 18:36:47
- Device serial: CR0886100SN
- HEAD SHA: 34d11e61aff07b7d206e7fb0641c686341fb36b

## Command

```
build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 8
```

## Result

- Exit code: `1`
- Verdict: **BLOCKED**
- Raw log: `2026-10-21-plan184-prop192-ir-rerun.raw.log` (same directory)
- Interposer confirmation: n/a — links no vendor SDK

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Connect to port 10013 (IR RPC) | **[FAIL]** `cannot connect to 127.0.0.1:10013 (connect: Connection refused; is RPCServer running?)` |
| 2 | ACCEPT/REJECT lines present for prop=192 | **[NOT REACHED]** blocked at connection |

## Crash sweep

hw-postflight: no crash report for 'raptor_client' at/after 2026-10-21 18:36:47 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## On failure or block — observable + one lead

- Observable: `build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 8`, exit 1; `[live] cannot connect to 127.0.0.1:10013 (connect: Connection refused; is RPCServer running?)` — IR RPC port 10013 not accepting connections at run time, despite RPCServer reported running.
- Lead: likely RPCServer's IR sub-service on port 10013 is not bound or has stopped — check RPCServer process and whether port 10013 is listening.

## Open questions

- Is port 10013 bound by RPCServer at all, or is it a separate process that may not have started?
- Was port 10013 available earlier in the session (the depth test used port 10010 successfully at the TCP level — start_stream succeeded)?
- Does the raw_capture --probe --match run (which preceded this test) affect the IR RPC service availability?

## Board

- Row move: #184 `HW` → `HW` — blocked: IR RPC port 10013 connection refused; prop=192 ACCEPT/REJECT check not reached; re-run needed with IR port confirmed listening
