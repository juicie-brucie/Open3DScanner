# PLAN #127 — depth port, second attempt after a confirmed-good connection check — HUNG again, not retried further

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: `fc6a081`
- Target: build/tools/raptor_client/raptor_client (build-tree copy)
- Preflight: pwd OK; no conflicting process; freshness: binary 2026-10-20 03:54:34 >= newest source main.cpp 03:54:28
- Interposer: n/a — links no vendor SDK
- Run start: 2026-10-20 17:55:26
- Device: Creality Raptor Pro, daemon running, device enumerated, no other SDK user
- Context: the user confirmed the physical connection directly before this
  retry (after three anomalies earlier this session: this same depth hang,
  an IR-capture hang, and a device-enumeration race/crash). This was the
  one authorized retry per that confirmation — not a blind repeat.

## Command

```
./build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 5 --dump-replies
```

## Result — HUNG again, identical signature to the first attempt

- Exit code: `143` (`SIGTERM`, sent by this agent after confirming a
  genuine stall, not a slow start)
- **Observable**: zero bytes of output for the full observation window
  (raw log confirmed 0 bytes), process alive but idle (`0:00.00` CPU),
  identical to the first depth-port attempt
  (`2026-10-20-plan127-live-reply-dump-retry.md`) — even after the
  physical connection was independently checked and confirmed good
  immediately before this run.
- Raw log: `2026-10-20-plan127-depth-retry2-hang.raw.log` (0 bytes)
- **Not retried further**, per the standing no-retry-on-hang rule and the
  coordinator's explicit instruction to stop after one retry if it hangs
  again.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-20-plan127-depth-retry2-hang '2026-10-20 17:55:26' raptor_client`:

> hw-postflight: no crash report for 'raptor_client' at/after 2026-10-20 17:55:26 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## Open questions

- This is now **two-for-two** hangs on the depth port (10010) specifically
  via `raptor_client --live`, with a clean IR (10013) success in between
  on the same binary/daemon. The physical connection was independently
  confirmed good before this second attempt, which rules out a loose
  cable/power issue as the sole explanation — whatever this is looks more
  likely to be specific to the depth interface's RPC path in
  `raptor_client`, or to the daemon's handling of it, than to the
  physical link. Not diagnosed further here — this is a strong, repeatable
  signal for a builder/recon-level look at the depth-interface connect/
  start_stream sequence specifically.

## Board

- Row move I would propose for **#127**: IR stays proposed for `DONE`
  (per the earlier successful run); **depth is now a distinct, repeatable
  blocker** worth its own follow-up row rather than another hardware
  retry — two clean attempts both hung identically with no output at all,
  which points at the client/daemon's depth-path code, not at
  environmental flakiness.
