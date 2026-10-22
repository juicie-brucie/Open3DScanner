# PLAN #71 — attempt 11 pre-run port check — PASS

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD SHA: 34d11e61aff07b7d206e7fb0641c686341fb236b
- Target: n/a — port-check only, no binary executed
- Other copy: n/a
- Preflight: n/a — port-check precedes preflight
- Interposer: n/a
- Run start: 2026-10-21 19:20:47

## Command

```
pgrep -l RPCServer; lsof -iTCP -sTCP:LISTEN -nP | grep -E '10010|10011|10013'
# (lsof without sudo shows user-accessible ports; netstat -an used to confirm root-owned ports)
netstat -an 2>/dev/null | grep -E '\.10010|\.10011|\.10013'
```

## Result

- Exit code: `0`
- Verdict: **PASS**
- Raw log: `2026-10-21-plan71-attempt11-portcheck.raw.log` (same directory)
- Interposer confirmation: n/a

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | RPCServer process running | **[PASS]** `53211 RPCServer` (PID 53211, running as root via `/Library/Creality/RPCServer`) |
| 2 | Port 10010 bound TCP LISTEN | **[PASS]** `tcp4 0 0 127.0.0.1.10010 *.* LISTEN` |
| 3 | Port 10011 bound TCP LISTEN | **[PASS]** `tcp4 0 0 127.0.0.1.10011 *.* LISTEN` |
| 4 | Port 10013 bound TCP LISTEN | **[PASS]** `tcp4 0 0 127.0.0.1.10013 *.* LISTEN` |
| 5 | hw-preflight.sh scan_server passes | **[PASS]** `hw-preflight: OK`; freshness binary 12:53:07 >= newest source main.cpp 12:52:55; RPCServer(53211,) running |

## Crash sweep

No process was executed; postflight not applicable.

## Open questions

- None — all three ports bound, RPCServer running; proceed to A/B test (attempt 11).

## Board

- Row move: #71 stays `HW` — port check passes; A/B test attempt 11 to follow in a separate record.
