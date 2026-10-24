# PLAN #198 — IOUSBHost UVC capture, post-lookupfix main retest — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: (detached) origin/main @ 2758c3e
- Target: build/tools/raw_capture/raw_capture  (explicit argument (the binary this run will execute))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 06:05:32 >= newest source descriptor_probe.cpp 06:05:22
- Interposer: expect first line: [launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib
- Run start: 2026-10-24 06:05:36

## Context

Fresh power-cycle immediately before this run. HEAD on main is 2758c3e (PR #502). PR #501 (commit 95307e2, IOUSBHost lookup fix) confirmed present in `git log origin/main`. Stash: `stash@{0}: On hw/next: hw room before #198 retry`.

## Commands

```
# Step 3 — preconditions
osascript -e 'quit app "CrealityScan"'       # rc=0
launchctl bootout gui/<uid>/com.creality.RPCServer  # rc=3 (already gone)
pgrep -x RPCServer                           # rc=1 (empty — no RPCServer)
ioreg -p IOService -n "Creality Raptor Pro" -l -r | grep -E 'UsbExclusiveOwner|bInterfaceNumber|IOClass'

# Step 4
build/tools/raw_capture/raw_capture --probe
build/tools/raw_capture/raw_capture --diag-interfaces
OPENSCANNER_UVC_IOUSBHOST=1 build/tools/raw_capture/raw_capture \
    --capture --stream depth --sink /tmp/uvc-depth-host2.rcap --max-frames 30 --max-seconds 10
```

## Result

- Exit code: `2`
- Verdict: **FAIL**
- Raw log: `2026-10-24-plan-uvc-iousbhost-lookupfix.raw.log` (same directory)
- Interposer confirmation: not applicable — raw_capture is a standalone tool (no Vulkan/OBScan context)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | PR #501 commit 95307e2 present on origin/main | **[PASS]** `git log origin/main` shows `95307e2 raw_capture: fix the modern IOUSBHost interface lookup (#198)` |
| 2 | origin/main at or after 2758c3e | **[PASS]** `git rev-parse origin/main` = `2758c3e52cdb59e08775c4e1c83295d41b340bc6` |
| 3 | Binary fresher than sources after build | **[PASS]** preflight: binary 06:05:32 >= newest source 06:05:22 |
| 4 | RPCServer absent before run | **[PASS]** `pgrep -x RPCServer` rc=1, empty |
| 5 | Depth stream interface 1 visible in IOUSBHostInterface enumeration | **[PASS]** `[uvc] [7] vid=0x2BC5 pid=0x0C03 interface=1  <-- match` |
| 6 | Depth capture: interface 1 seized, frames > 0 | **[FAIL]** exit 2; `interface 1 could not be seized from its current owner: Failed to create IOUSBHostInterface. (retried 15 times over ~1500ms)` |
| 7 | IR capture | **[SKIP]** not attempted — depth did not pass |

## Preconditions verbatim

**ioreg — UsbExclusiveOwner / bInterfaceNumber / IOClass:**
```
|     "IOClass" = "AppleUSBHostCompositeDevice"
  | |   "UsbExclusiveOwner" = "pid 603, UVCAssistant"
  | |   "bInterfaceNumber" = 0
  | |   "UsbExclusiveOwner" = "pid 603, UVCAssistant"
  | |   "bInterfaceNumber" = 1
  | |   "UsbExclusiveOwner" = "pid 603, UVCAssistant"
  | |   "bInterfaceNumber" = 2
  | |   "UsbExclusiveOwner" = "pid 603, UVCAssistant"
  | |   "bInterfaceNumber" = 3
  | |   "UsbExclusiveOwner" = "pid 603, UVCAssistant"
  | |   "bInterfaceNumber" = 4
  | |   "UsbExclusiveOwner" = "pid 603, UVCAssistant"
  | |   "bInterfaceNumber" = 5
    |   "UsbExclusiveOwner" = "AppleUserUSBHostHIDDevice"
    |   "bInterfaceNumber" = 6
```

**--diag-interfaces verdict:**
```
[diag] total IOUSBHostInterface nodes visible: 14; of those, scanner's: 7
[diag] VERDICT: the scanner's interfaces ARE visible to this process (7 found) — the blocker is in the matching filter or the seize, not raw visibility.
```
Scanner interfaces 0–5: owner `pid 603, UVCAssistant`. Interface 6: `AppleUserUSBHostHIDDevice`.

**--probe summary:** 3 video streams found:
- stream[0]: interface=1, ep=0x81, bulk, depth Y8 480×300, max_frame_bytes=252000
- stream[1]: interface=3, ep=0x82, bulk, IR Y8 960×600, max_frame_bytes=576000
- stream[2]: interface=5, ep=0x83, bulk, RGB YUY2 1280×800, max_frame_bytes=2048000

## Capture results

**Depth capture** (`--stream depth`, exit 2):
```
[capture] sink: /tmp/uvc-depth-host2.rcap (16 bytes)
[capture] stop: source-error
[capture] detail: interface 1 could not be seized from its current owner: Failed to create IOUSBHostInterface. (retried 15 times over ~1500ms)
[capture] frames: 0 (ir 0, depth 0, rgb 0), 0 record bytes
```
Sink: 16 bytes (header only, no frames).

**IR capture:** not attempted (depth prerequisite failed).

## Restore

`git checkout hw/next` — switched to hw/next. `git stash pop` — stash@{0} dropped, working tree clean (untracked files only).

## Crash sweep

hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 06:05:36 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure — observable + one lead

- Observable: `OPENSCANNER_UVC_IOUSBHOST=1 build/tools/raw_capture/raw_capture --capture --stream depth --sink /tmp/uvc-depth-host2.rcap --max-frames 30 --max-seconds 10` — exit 2 — `interface 1 could not be seized from its current owner: Failed to create IOUSBHostInterface. (retried 15 times over ~1500ms)` — interface IS visible in enumeration (match logged at index [7]) but IOUSBHostInterface object creation fails at seize time.
- Lead: likely in the interface-seize / `IOUSBHostInterface` open call (`tools/raw_capture/iokit_source.mm`) — UVCAssistant holds an exclusive claim on interface 1, and the lookup fix (PR #501) resolved enumeration but the seize against an exclusively-owned interface remains unresolved.

## Open questions

- Does UVCAssistant release its claim if the user unplugs and replugs the device without launching CrealityScan — would that change the `UsbExclusiveOwner` field?
- Is there a `kIOUSBHostInterfaceOpenOption` or entitlement that allows displacing a UVCAssistant claim, and does raw_capture request it?
- Was UVCAssistant's exclusive claim present in the earlier passing run, or did this run encounter a different device state?

## Board

- Row move: #198 `HW` → `FAIL` — IOUSBHost lookup fix resolved enumeration (interface found at index [7]) but seize still blocked by UVCAssistant's exclusive ownership of interfaces 0–5.
