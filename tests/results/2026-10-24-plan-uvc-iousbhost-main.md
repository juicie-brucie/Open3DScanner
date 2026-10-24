# Modern IOUSBHost UVC capture run (main) — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (detached to origin/main for build; restored to hw/next before this record was written)
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 05:01:04 >= newest source iokit_source.mm 03:28:29
- Interposer: expect first line: [launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib — NOT present (raw_capture run directly, not via launcher)
- Run start: 2026-10-24 05:15:07

## Setup

- Stash ref: stash@{0} "hw room before main checkout" (dropped on restore: 4e2af3ac84833881e03019044da9814e4f9c5daa)
- HEAD (main): e7fa821145c5ef47fcf21432aed969722eb0a079 (includes PR #489, #491, #490)
- Binary sha256: `90434c75a1385df60209553689000910ed9b907551c4cc4a1cb378050d3e5785`
- Fresh power-cycle: device power-cycled immediately before this run (coordinator note)

## Command

```
OPENSCANNER_UVC_IOUSBHOST=1 build/tools/raw_capture/raw_capture \
    --capture --stream depth --sink /tmp/uvc-depth-host.rcap --max-frames 30 --max-seconds 10
```

## Result

- Exit code: `2`
- Verdict: **FAIL**
- Raw log: `2026-10-24-plan-uvc-iousbhost-main.raw.log` (same directory)
- Interposer confirmation: not applicable — raw_capture run directly without launcher

## Preconditions

- `osascript -e 'quit app "CrealityScan"'`: rc 0
- `launchctl bootout gui/.../com.creality.RPCServer`: rc 3 (service not loaded)
- `pgrep -x RPCServer`: rc 1 (not running)
- ioreg UsbExclusiveOwner: interfaces 0-5 owned by `pid 603, UVCAssistant`; interface 6 owned by `AppleUserUSBHostHIDDevice`; top-level IOClass `AppleUSBHostCompositeDevice`

## --probe summary

3 video streams found (exit 0):
- stream[0]: interface 1, endpoint 0x81, bulk, Y8 480x300 max_frame_bytes=252000
- stream[1]: interface 3, endpoint 0x82, bulk, Y8 960x600 max_frame_bytes=576000
- stream[2]: interface 5, endpoint 0x83, bulk, YUY2 1280x800 max_frame_bytes=2048000

HID interface 6: input-only (62 fields, 248 bytes/report); no output capability.

## --diag-interfaces

14 IOUSBHostInterface nodes visible; 7 are scanner's (vid=0x2BC5 pid=0x0C03):
- interfaces 0, 1, 2, 3, 4, 5: owner `pid 603, UVCAssistant`
- interface 6: owner `AppleUserUSBHostHIDDevice`

Tool verdict: `the scanner's interfaces ARE visible to this process (7 found) — the blocker is in the matching filter or the seize, not raw visibility.`

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | --probe enumerates 3 video streams, exit 0 | **[PASS]** 3 streams, exit 0 |
| 2 | --diag-interfaces sees 7 scanner interfaces | **[PASS]** 7 scanner interfaces visible |
| 3 | Depth capture (IOUSBHost path) captures >0 frames, exit 0 | **[FAIL]** exit 2; `[capture] stop: source-error`; `[capture] detail: interface 1 is not on the device (retried 15 times over ~1500ms)`; frames: 0 |
| 4 | Classic root diagnostic (sudo) runs | **[FAIL]** sudo refused non-interactively: `sudo: a terminal is required to read the password`; exit 1 |

## Crash sweep

hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 05:15:07 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure or block — observable + one lead

- Observable: `OPENSCANNER_UVC_IOUSBHOST=1 build/tools/raw_capture/raw_capture --capture --stream depth --sink /tmp/uvc-depth-host.rcap --max-frames 30 --max-seconds 10` exited 2 with: `[capture] stop: source-error` / `[capture] detail: interface 1 is not on the device (retried 15 times over ~1500ms)`. Frames captured: 0. Sink written: 16 bytes (header only). --diag-interfaces confirms interface 1 is visible but owned by `pid 603, UVCAssistant`. Classic root sudo was refused non-interactively (exit 1).
- Lead: likely the IOUSBHost seize of interface 1 fails because UVCAssistant holds exclusive ownership; the "not on the device" error is the seize rejection (tools/raw_capture/iokit_source.mm).

## Open questions

- Does the IOUSBHost path attempt to seize the interface while UVCAssistant holds it, and if so does it have a mechanism to request UVCAssistant yield?
- Would running as root (with interactive sudo) allow the seize to succeed, given UVCAssistant exclusivity?
- Is "interface 1 is not on the device" the IOKit error for a failed seize/open, or a separate enumeration failure?
- Does UVCAssistant release the interface when no camera app is active, and would a different power-cycle sequence (with no CoreMedia initialization) change the owner?

## Restore confirmation

- Branch: hw/next (confirmed, Switched to branch 'hw/next')
- Stash pop: succeeded (Dropped refs/stash@{0}: 4e2af3ac84833881e03019044da9814e4f9c5daa)
- git status: untracked files only, working tree clean

## Board

- Row move: none — this is an evidence run; board disposition belongs to the dispatcher.
