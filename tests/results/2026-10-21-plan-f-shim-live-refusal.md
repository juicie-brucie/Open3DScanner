# PLAN F — raptor_shim --source live refusal (not runnable yet) — BLOCKED

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_shim/raptor_shim (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Sha256: `c8f0dad26ff1f008b891923908c2055d0fdaf528200c26e9d38e4b5a75863d53`
- HEAD: `e7e1ad200b4ff10a86008ec723bd53ff78503e62`
- Other copy: none
- Preflight: pwd OK; no conflicting process; freshness: binary 21:23:57 >= newest source live_source.cpp 20:52:26
- Interposer: n/a — `raptor_shim` links no vendor SDK; exempt per preflight
- Run start: 2026-10-21 22:23:04
- Peer: our shim (raptor_shim); no helper claim held and no vendor RPC involved — this step exercises the shim's own refusal path

## Command

```
build/tools/raptor_shim/raptor_shim --source live --seconds 2
```

## Result

- Exit code: `2`
- Verdict: **BLOCKED** (expected refusal per the tool's own `--help`: "live is refused cleanly until the #178 transport and the helper's claim exist; nothing is guessed")
- Raw log: `2026-10-21-plan-f-shim-live-refusal.raw.log`

## On failure or block — observable + one lead

- Observable: `build/tools/raptor_shim/raptor_shim --source live --seconds 2` → exit `2`, stderr verbatim:
  `raptor_shim: live source refused: not-configured: the device is not claimed by openscanner-helper (openscanner-helper is not answering on org.openscanner.helper (reply was not a dictionary: no Mach service is registered under that name in this session (install/start the openscanner-helper user LaunchAgent: tools/macos_helper/install_user_agent.sh install))); not-configured: this refusing source has no live backend; wire MakeLiveUvcSource (raw_capture --capture) with descriptor_table.h configured (its facts come from docs/recon/findings/usb-descriptor-map.md and the probe record); not-configured: e10 (the IR frame's IR light type) has no source: the UVC transport does not report the device's light state, and no finding binds the laser/exposure state to it`
  This is the documented, clean refusal — no vendor path was substituted.
- Lead: n/a — this is the tool's designed refusal, not a defect; nothing to lead a fixer to.

## Crash sweep

`scripts/hw-postflight.sh plan-f-shim-refusal '2026-10-21 22:23:04' raptor_shim` →
`hw-postflight: no crash report for 'raptor_shim' at/after 2026-10-21 22:23:04 — safe to record 'no crash report (checked DiagnosticReports for raptor_shim at/after the run start)'.`

## Open questions

- At this run, the helper user-agent from `2026-10-21-plan-b-nosudo-claim.md` had already been uninstalled (quit/release) — does the refusal text change if `--source live` is attempted while the no-sudo helper is actively claiming (`claimed-by-helper`)? Not tested in this run.

## Board

- Row move: none proposed by this record — the vendor↔shim live A/B stays not runnable; this record only documents the exact refusal text.
