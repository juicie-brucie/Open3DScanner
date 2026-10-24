# #198 — root interface-claim diagnostic (can root's DeviceSeize displace UVCAssistant?) — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 06:19:44 >= newest source descriptor_probe.cpp 06:06:04
- Interposer: expect first line: [launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib
- Run start: 2026-10-24 07:31:14

## Preconditions

- Vendor daemon: not running (`pgrep -ifl "orbbec|obscan|UVCAssistant|scan_server|openscanner-helper"` found only the standing macOS `UVCAssistant` system extension, pid 603 — not an Orbbec/vendor process).
- Device: operator confirmed power-cycled immediately before this run (not independently verified by this agent — no power-cycle control is in scope).
- Helper: confirmed uninstalled — `tools/macos_helper/install_user_agent.sh status` reports `org.openscanner.helper is not registered in gui/501`.

## Command

Run as **root**, in an interactive terminal tab (sudo needs the operator's password; this agent does not hold or enter it):

```
sudo OPENSCANNER_UVC_IOUSBHOST=1 build/tools/raw_capture/raw_capture \
    --capture --stream depth --sink /tmp/uvc-depth-root.rcap --max-frames 30 --max-seconds 10
```

## Result

- Exit code: not independently captured — the command ran in an interactive terminal tab the operator typed their password into, and this agent has no channel to read `$?` from that same shell afterward without typing into it itself (typing into a tab the operator has used is refused). The program's own verbatim stdout below is the record of what happened.
- Verdict: **FAIL**
- Raw log: `2026-10-24-plan198-root-interface-claim.raw.log` (same directory)
- Interposer confirmation: n/a — `raw_capture` does not create an OBScan context (stated in preflight's own output); not applicable to this target

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | ioreg interface owners (`bInterfaceNumber` 0–5) before the run | **[PASS]** all six `"UsbExclusiveOwner" = "pid 603, UVCAssistant"` |
| 2 | root's capture (via `sudo` + `OPENSCANNER_UVC_IOUSBHOST=1`) opens the device and displaces `UVCAssistant`'s interface claim | **[FAIL]** `[capture] stop: source-error` / `[capture] detail: interface 1 is not on the device (retried 15 times over ~1500ms)`; `frames: 0 (ir 0, depth 0, rgb 0), 0 record bytes` |
| 3 | ioreg interface owners (`bInterfaceNumber` 0–5) after the run | **[FAIL]** unchanged — all six still `"UsbExclusiveOwner" = "pid 603, UVCAssistant"`; root's run did not acquire, release, or otherwise alter any interface owner |

Note the failure mode differs from the non-root run against the same device/interfaces in the prior card (`2026-10-24-plan181-helper-claim-capture.md`): that one failed at device-open with `Failed to create IOUSBHostObject` / `stop the vendor daemon: it holds the device`; this root run instead got past that stage and failed later with `interface 1 is not on the device (retried 15 times over ~1500ms)` — a different, more specific error, though the ioreg owner state never changed in either run.

## Crash sweep

hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 07:31:14 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure or block — observable + one lead

- Observable: running as root with `OPENSCANNER_UVC_IOUSBHOST=1` set, `raw_capture --capture --stream depth` failed with `interface 1 is not on the device (retried 15 times over ~1500ms)`, 0 frames; the before/after `ioreg` dumps of `UsbExclusiveOwner` for `bInterfaceNumber` 0–5 are byte-for-byte identical (`pid 603, UVCAssistant` throughout).
- Lead: likely in whatever `OPENSCANNER_UVC_IOUSBHOST=1`'s interface-1 lookup does after device open (`tools/raw_capture/iokit_source.mm` per the preflight's freshness check) — root privilege changed the failure point (past device-open, into interface enumeration) but never touched the `UsbExclusiveOwner` IOKit state that `UVCAssistant` holds.

## Open questions

- Does `interface 1 is not on the device` mean the device re-enumerated with a different interface layout under root's open attempt, or does it mean the code's interface-1 lookup is wrong/stale regardless of privilege (a correctness question, not a privilege one)?
- Per the card's own settle criterion: the ioreg owner never moved under root, so does this settle "no → the raw-USB path needs a dext/entitlement," or does the changed (but still-failing) error at interface lookup mean the privilege question is not yet cleanly answered — i.e. is a privilege-driven `DeviceSeize` even being attempted by this code path, or is `sudo` here just running the same open call with root's ambient authority and no explicit seize?

## Board

- Row move: #198 stays open/`HW` pending the dispatcher's read of the open questions above — the observable itself (ioreg owner unchanged under root) supports the card's "no" branch (root's elevated privilege alone did not displace `UVCAssistant`'s interface claim), but the shifted failure point (device-open passed; interface-1 lookup failed) is a new, unexplained observable this agent is not positioned to resolve.
