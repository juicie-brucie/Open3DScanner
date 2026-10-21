# PLAN #170 — USB cable pull while device powered (unplug-release variant) — PASS

Addendum to `2026-10-21-plan170-helper-claim.md` and the caveat in
`2026-10-21-plan170-unplug-caveat.md`. The earlier step 4 fired on device
power-down; this run isolates the cold cable pull with the scanner powered on.

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/macos_helper/openscanner-helper  (build-tree copy; binary 12:53)
- Preflight: continued session; RPCServer booted-out (launchctl bootout from earlier); no conflicting process
- Run start: ~2026-10-21 14:55 (same session; postflight uses 14:32:23 start)
- Device: CR-Scan Raptor Pro; **device powered on throughout**; USB cable pulled cold by user while helper was serving

## Command

```
sudo build/tools/macos_helper/openscanner-helper --claim iokit
# [user pulled USB cable while device remained powered]
```

## Full output

```
helper: xpc: listening on Mach service org.openscanner.helper
helper: xpc: listener error: Connection invalid (a Mach service not registered
  with launchd reports this; the claim and its release still run)
helper: claim: start
helper: claim: idle -> claiming (start)
helper: claim: claiming -> claimed (device=iokit-usb)
helper: start: claim: ok=true state=claimed (claimed via iokit-usb)
helper: start: serving (no data path; this build carries no streaming)
helper: claim: claimed -> releasing (unplug)
helper: claim: releasing -> released (unplug: release complete)
```

Helper remained running after release (expected — no re-claim on reconnect in
this row). Killed by agent to end the run.

## Checks

| # | Check | Result |
|---|---|---|
| 1 | Claim succeeds (no vendor daemon) | **[PASS]** `claiming -> claimed (device=iokit-usb)` |
| 2 | Serving state reached | **[PASS]** `start: serving` |
| 3 | Cold cable pull triggers IOKit removal event | **[PASS]** `claimed -> releasing (unplug)` fires without any signal |
| 4 | Release completes cleanly | **[PASS]** `releasing -> released (unplug: release complete)` |
| 5 | No crash | **[PASS]** postflight: no crash report |

## Caveat resolved

The prior caveat (`2026-10-21-plan170-unplug-caveat.md`) noted that step 4 of
the gate run fired on device power-down, not a cold cable pull. This run confirms
the IOKit service-termination interest fires on a **cold USB cable pull with the
device powered on** — the same `claimed -> releasing (unplug) -> released` sequence,
same log lines.

Both removal triggers (power-off and cold cable pull) produce the same
state-machine path via the IOKit termination-interest handler.

## Crash sweep

`scripts/hw-postflight.sh plan170-usb-unplug '2026-10-21 14:32:23' openscanner-helper` →
`hw-postflight: no crash report for 'openscanner-helper' at/after 2026-10-21 14:32:23 — safe to record 'no crash report (checked DiagnosticReports for openscanner-helper at/after the run start)'.`

## Open questions

- The helper stays running after the unplug release. On replug, a new IOKit
  notification arrives — does the existing helper see it, or is a restart or
  explicit XPC `claim` required to re-claim?

## Board

- Row move: #170 `HW` → `DONE` — the cold-cable-pull path is now confirmed.
  Combined with the original gate record (`2026-10-21-plan170-helper-claim.md`),
  all four gate steps plus the powered-unplug variant are confirmed on real hardware.
