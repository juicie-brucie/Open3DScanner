# PLAN #71 — A/B plane removal test (runtime toggle) — BLOCKED

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: (from hw worktree)
- Target: build/tools/scan_server/scan_server
- Other copy: (not reached — preflight blocked)
- Preflight: FAIL — hw-preflight.sh exited 1
- Interposer: not reached
- Run start: 2026-10-21 19:10:00

## Command

```
scripts/hw-preflight.sh scan_server
```

## Result

- Exit code: `1`
- Verdict: **BLOCKED**
- Raw log: `2026-10-21-plan71-attempt10-ab.raw.log` (same directory)
- Interposer confirmation: ABSENT — stopped at preflight

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Preflight gate passes | **[FAIL]** `hw-preflight: FAIL — the CrealityScan RPC daemon host is not running (no 'CrealityScan' or 'RPCServer' process) — fix: start CrealityScan (macOS: the 'com.creality.RPCServer' LaunchAgent; the CrealityScan app counts as the RPC host too).` |

## Crash sweep

Run start was 2026-10-21 19:10:00; scan_server never launched. Postflight not run — no process executed.

## On failure or block — observable + one lead

- Observable: `scripts/hw-preflight.sh scan_server` exited 1 with message: `hw-preflight: FAIL — the CrealityScan RPC daemon host is not running (no 'CrealityScan' or 'RPCServer' process) — fix: start CrealityScan (macOS: the 'com.creality.RPCServer' LaunchAgent; the CrealityScan app counts as the RPC host too).`
- Lead: hw-preflight.sh RPC daemon check — the check fires even when scan_server does not require RPCServer (the task brief states RPCServer is intentionally stopped for this OBScan SDK test)

## Open questions

- Does scan_server actually require the CrealityScan RPC daemon to be running, or does hw-preflight.sh apply that check unconditionally to all OBScan SDK targets regardless of whether they use RPCServer?
- Should the preflight gate skip the RPC daemon check for scan_server invocations that pass `--live` without an RPCServer dependency?

## Board

- Row move: #71 `HW` → `HW` — blocked at preflight; gate requires RPC daemon running but task brief states RPCServer is intentionally stopped
