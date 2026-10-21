# PLAN #170 — privileged helper live claim/release — BLOCKED

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/macos_helper/openscanner-helper  (build-tree copy (fixed order 3: build/tools/*/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a; freshness: binary 12:53 >= source 12:52 (verified by hand — preflight reports source dir not auto-found)
- Interposer: n/a — `openscanner-helper` is a standalone IOKit daemon; does not use the launcher or vendor SDK interposer
- Run start: 2026-10-21 14:32:23
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`; power-cycled; Creality Scan closed; RPCServer still running (LaunchDaemon, persists after app close)

## What the gate requires (from tools/macos_helper/README.md)

Four steps, each run as root (`sudo`):

1. **Refusal (vendor daemon running):** `sudo openscanner-helper --once` →
   expect `ok=false state=released` — the non-seizing default refuses rather than
   killing the vendor daemon's claim.
2. **Claim (vendor daemon stopped):** `sudo openscanner-helper --once` →
   expect `ok=true state=claimed` then `releasing -> released`, exit 0.
3. **Claim and hold, release on quit:** `sudo openscanner-helper --claim iokit` →
   expect `claiming -> claimed` then `start: serving`; on SIGTERM expect
   `claimed -> releasing (quit)`, exit 0.
4. **Release on unplug:** Same command as step 3, leave serving, unplug USB →
   expect `claimed -> releasing (unplug)`, `releasing -> released (unplug: release
   complete)`.

## Observable

```
$ sudo build/tools/macos_helper/openscanner-helper --once
sudo: a terminal is required to read the password; either use the -S option to
read from standard input or configure an askpass helper
sudo: a password is required
EXIT:1
```

The non-interactive shell used by this agent cannot authenticate `sudo`. None of
the four gate steps ran.

## Crash sweep

`scripts/hw-postflight.sh plan170-helper-claim '2026-10-21 14:32:23' openscanner-helper` →
`hw-postflight: no crash report for 'openscanner-helper' at/after 2026-10-21 14:32:23 — safe to record 'no crash report (checked DiagnosticReports for openscanner-helper at/after the run start)'.`

## Open questions

- Can the operator gate be run interactively by the user directly (steps 1–4 from a
  terminal with `sudo` available)?
- Is there a `sudoers` entry or alternative privilege path that would allow the
  binary to be run without interactive authentication from this agent's shell?

## Board

- Row move: #170 stays `HW` — the gate requires `sudo` root execution; the
  non-interactive agent shell cannot authenticate. No claim attempt reached the
  hardware.

## Lead

Lead: the gate is structurally correct but requires interactive root access — the
operator must run steps 1–4 from their own terminal session.
