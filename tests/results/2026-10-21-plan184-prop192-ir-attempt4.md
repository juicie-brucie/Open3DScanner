# PLAN #184 — prop=192 ACCEPT/REJECT check — BLOCKED

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_client/raptor_client  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 18:21:15 >= newest source main.cpp 18:21:07
- Interposer: n/a — links no vendor SDK (socket/STL only); vendor-artifact & interposer rules do not apply
- Run start: 2026-10-21 19:33:32

## Command

```
lsof -iTCP -sTCP:LISTEN -nP | grep -E '10010|10013'
```
(pre-check; run command never issued — see block below)

## Result

- Exit code: n/a (blocked before execution)
- Verdict: **BLOCKED**
- Raw log: `2026-10-21-plan184-prop192-ir-attempt4.raw.log` (same directory)
- Interposer confirmation: n/a

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Port 10013 listening (first check) | **[FAIL]** `lsof` returned no output — port not bound |
| 2 | Port 10013 listening (second check, after 5 s) | **[FAIL]** `lsof` returned no output — port still not bound |

## Crash sweep

hw-postflight: no crash report for 'raptor_client' at/after 2026-10-21 19:33:32 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## On failure or block — observable + one lead

- Observable: `lsof -iTCP -sTCP:LISTEN -nP | grep -E '10010|10013'` returned no output on both checks (first and after 5 s delay). Port 10013 is not bound. The test command was never issued.
- Lead: RPCServer process likely not running or failed to bind after the bootout+bootstrap cycle; check launchd service status for the RPCServer daemon.

## Open questions

- Is the RPCServer launchd job still loaded and running after the bootout+bootstrap? (`launchctl list | grep <rpcserver-label>`)
- Did RPCServer exit or crash between the reported restart and this run?

## Board

- Row move: #184 `HW` → `HW` — blocked: port 10013 not bound, test not executed; RPCServer needs to be confirmed running before re-run.
