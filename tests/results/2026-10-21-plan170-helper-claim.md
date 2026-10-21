# PLAN #170 — privileged helper live claim/release gate — PASS

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/macos_helper/openscanner-helper  (build-tree copy (fixed order 3: build/tools/*/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a; freshness: binary 12:53 >= source 12:52 (verified by hand — preflight reports source dir not auto-found); interposer: n/a — standalone IOKit daemon, no vendor SDK, no launcher
- Run start: 2026-10-21 14:32:23
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`; power-cycled; USB connected throughout steps 1–3; unplugged in step 4

## Context

Earlier attempt (`2026-10-21-plan170-helper-claim-blocked.md`) was blocked: sudo
required interactive authentication. Coordinator granted passwordless sudo for the
helper binary path. All four README operator-gate steps then ran.

## Commands (four steps)

```
# Step 1 (RPCServer running):
sudo build/tools/macos_helper/openscanner-helper --once

# Step 2 (RPCServer stopped via launchctl bootout):
sudo build/tools/macos_helper/openscanner-helper --once

# Step 3 (SIGTERM after 4 s):
sudo build/tools/macos_helper/openscanner-helper --claim iokit
# [SIGTERM sent]

# Step 4 (USB cable unplugged by user while serving):
sudo build/tools/macos_helper/openscanner-helper --claim iokit
# [user unplugged USB cable]
```

## Step-by-step results

### Step 1 — non-seizing refusal (RPCServer PID 76444 running)

```
helper: claim: idle -> claiming (start)
helper: claim: claiming -> released (claim failed: the USB device open was
  refused (0xe00002c5): another process holds the scanner
  (stop it: that is the operator gate))
helper: once: claim: ok=false state=released (...)
helper: claim: released (no claim held, backend not called)
helper: once: release: ok=true state=released (no claim held)
```

Exit 1. RPCServer confirmed still running after (`pgrep -x RPCServer` returned 76444).

### Step 2 — clean claim (RPCServer gone after `launchctl bootout`)

```
helper: claim: idle -> claiming (start)
helper: claim: claiming -> claimed (device=iokit-usb)
helper: once: claim: ok=true state=claimed (claimed via iokit-usb)
helper: claim: claimed -> releasing (quit)
helper: claim: releasing -> released (quit: release complete)
helper: once: release: ok=true state=released (released on quit)
```

Exit 0.

### Step 3 — claim and hold, release on SIGTERM

```
helper: claim: idle -> claiming (start)
helper: claim: claiming -> claimed (device=iokit-usb)
helper: start: claim: ok=true state=claimed (claimed via iokit-usb)
helper: start: serving (no data path; this build carries no streaming)
helper: quit: signal
helper: claim: claimed -> releasing (quit)
helper: claim: releasing -> released (quit: release complete)
helper: quit: release: ok=true state=released (released on quit)
```

Exit 0. XPC listener note in all `--claim iokit` runs: `Connection invalid (a Mach
service not registered with launchd reports this; the claim and its release still
run)` — expected for foreground execution without launchd registration.

### Step 4 — claim and hold, release on unplug

```
helper: claim: idle -> claiming (start)
helper: claim: claiming -> claimed (device=iokit-usb)
helper: start: claim: ok=true state=claimed (claimed via iokit-usb)
helper: start: serving (no data path; this build carries no streaming)
helper: claim: claimed -> releasing (unplug)
helper: claim: releasing -> released (unplug: release complete)
```

USB cable unplugged by user while helper was serving. Helper transitioned
`claimed -> releasing (unplug) -> released (unplug: release complete)` without
any signal. Helper remained running after the unplug release (expected per README:
"the helper does not re-claim on its own in this row — claim-on-connect is later
work"). Killed by agent to end the step.

## Checks

| # | Check | Result |
|---|---|---|
| 1 | Non-seizing: RPCServer refused, not killed | **[PASS]** `ok=false state=released`; RPCServer still running after (same PID) |
| 2 | Clean claim with daemon stopped: `ok=true state=claimed` | **[PASS]** `claiming -> claimed (device=iokit-usb)` |
| 3 | Release on quit (`--once` path): `releasing -> released (quit: release complete)` | **[PASS]** exact sequence logged; exit 0 |
| 4 | Claim and hold: `start: serving` reached | **[PASS]** `start: serving (no data path; this build carries no streaming)` |
| 5 | Release on SIGTERM: `claimed -> releasing (quit) -> released` | **[PASS]** exact sequence logged; exit 0 |
| 6 | Release on unplug: `claimed -> releasing (unplug) -> released` | **[PASS]** `claimed -> releasing (unplug)`, `releasing -> released (unplug: release complete)` — no signal needed |
| 7 | No crash | **[PASS]** postflight: no crash report |

## Note on intermediate block

A prior blocked attempt (`2026-10-21-plan170-helper-claim-blocked.md`) recorded
the sudo constraint. This record supersedes it for the gate verdict; both files
are kept as the observable record of the session.

## Crash sweep

`scripts/hw-postflight.sh plan170-helper-claim '2026-10-21 14:32:23' openscanner-helper` →
`hw-postflight: no crash report for 'openscanner-helper' at/after 2026-10-21 14:32:23 — safe to record 'no crash report (checked DiagnosticReports for openscanner-helper at/after the run start)'.`

## Open questions

- After an unplug release the helper stays running but holds no claim. Is the
  expected operator workflow to send an XPC `claim` after replug, or to restart
  the helper? (Per README: both work; claim-on-connect is later work.)
- The XPC `Connection invalid` note appears in all foreground runs (not registered
  with launchd). Is the XPC path exercised only in the device-free `helper_xpc_test`,
  or is there a foreground path to test it without launchd registration?

## Board

- Row move: #170 `HW` → `DONE` — all four operator-gate steps confirmed on real
  hardware: non-seizing refusal (step 1), clean claim (step 2), quit-release
  (step 3), and unplug-release (step 4). The claim state machine transitions
  log exactly as specified in `tools/macos_helper/README.md`.
