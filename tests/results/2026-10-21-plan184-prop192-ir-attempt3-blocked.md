# PLAN #184 — prop=192 ACCEPT/REJECT check (IR RPC streaming) — BLOCKED

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_client/raptor_client  (explicit argument (the binary this run will execute))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 18:21:15 >= newest source main.cpp 18:21:07
- Interposer: n/a — links no vendor SDK (socket/STL only); vendor-artifact & interposer rules do not apply
- Run start: 2026-10-21 19:32:14

## Command

```
build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 8
```

(Not executed — pre-check gate failed before the command ran.)

## Result

- Exit code: n/a (command not run)
- Verdict: **BLOCKED**
- Raw log: `2026-10-21-plan184-prop192-ir-attempt3-blocked.raw.log` (same directory)
- Interposer confirmation: n/a

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Port 10013 listening before run | **[FAIL]** `lsof -iTCP -sTCP:LISTEN -nP \| grep 10013` returned no output |
| 2 | Ports 10010, 10011 also checked | **[FAIL]** all three RPC ports returned no output — RPCServer not running |

## Crash sweep

no crash report for 'raptor_client' at/after 2026-10-21 19:32:14 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## On failure or block — observable + one lead

- Observable: pre-check `lsof -iTCP -sTCP:LISTEN -nP | grep 10013` returned no output; follow-up check on ports 10010 and 10011 also returned no output; RPCServer process is not running; command was not executed
- Lead: RPCServer (scan_server --live or equivalent) needs to be started and its three RPC ports confirmed listening before this test can proceed

## Open questions

- Was RPCServer stopped since the 19:22 verification earlier in this session, and if so, what stopped it?
- Does the device still enumerate (USB/network)? If RPCServer crashed, a power cycle may be needed before the next attempt.
- Will the next attempt be attempt 4 with a fresh RPCServer start confirmed immediately before the raptor_client invocation?

## Board

- Row move: #184 `HW` → `HW` — BLOCKED; RPCServer not running at run time; no test data collected; re-queue after RPCServer is confirmed live
