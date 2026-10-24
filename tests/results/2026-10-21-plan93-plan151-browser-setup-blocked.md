# PLAN #93 / #151 — browser render + markerless HUD session setup — BLOCKED

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server (newest built copy; no other copy found)
- Other copy: none
- Preflight: FAILED (see below) — no run attempted
- Interposer: not reached
- Run start: not reached (preflight blocked before launch)

## Command

```
scripts/hw-preflight.sh scan_server
```

## Result

- Exit code: `1`
- Verdict: **BLOCKED**
- Raw log: none (preflight produced no header block; its stderr is quoted below verbatim)
- Interposer confirmation: not reached

```
hw-preflight: FAIL — the CrealityScan RPC daemon host is not running (no 'CrealityScan' or
'RPCServer' process) — fix: start CrealityScan (macOS: the 'com.creality.RPCServer'
LaunchAgent; the CrealityScan app counts as the RPC host too).
```

Device state observed independently (not part of preflight): `ioreg -p IOUSB -l` shows
"Creality Raptor Pro" (serial `CR0886100SN`) enumerated over USB at time of check, so the
scanner itself is physically connected; the block is purely the missing RPC host process.

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Preflight gate passes (worktree/branch, process check, binary freshness, RPC host) | **[FAIL]** RPC daemon host ('CrealityScan'/'RPCServer') not running |

## Crash sweep

Not run — no binary was launched.

## Open questions

- Will the operator start the CrealityScan app (or the `com.creality.RPCServer` LaunchAgent)
  before the next attempt?
- Once the RPC host is up, does preflight pass cleanly on retry?

Lead: n/a (environment/setup block, not a code defect) — the operator needs to start the
vendor RPC host process before any `scan_server --live` run can proceed.
