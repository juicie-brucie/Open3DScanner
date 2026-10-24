# Classic IOUSBLib UVC capture — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 03:28:34 >= newest source iokit_source.mm 03:28:29
- Interposer: expect first line: [launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib — NOT APPLICABLE: raw_capture is a pure IOUSBLib/IOKit tool, no launcher is used; interposer line was absent as expected
- Run start: 2026-10-24 05:00:54

HEAD SHA: `a1561c1840abc7776217c8e196752a8a8609fe6b`
Binary sha256: `692ab322292c2565ac03a9eb1b199e65f077e1eaf02e10a90f98366e85964aeb`

Device note: freshly power-cycled (unplugged 5 s, replugged) immediately before this run.

## Preconditions

- `osascript -e 'quit app "CrealityScan"'` rc: 0
- `launchctl bootout gui/<uid>/com.creality.RPCServer` rc: 0
- `pgrep -x RPCServer` → (empty), rc: 1 — no RPCServer process
- `ioreg` UsbExclusiveOwner/bInterfaceNumber lines:
  ```
  |     "bInterfaceNumber" = 0
    |     "bInterfaceNumber" = 1
    |     "bInterfaceNumber" = 2
    |     "bInterfaceNumber" = 3
    |     "bInterfaceNumber" = 4
    |     "bInterfaceNumber" = 5
          "bInterfaceNumber" = 6
  ```
  UsbExclusiveOwner field: absent from all scanner nodes.

## Commands and Results

### --probe (exit 0)

3 video-streaming interfaces found, all Y8/YUY2 bulk, plus 1 HID interrupt interface (7 total):

- stream[0] — interface 1, endpoint 0x81, bulk, Y8 (fourcc `Y8  `), 480×300, max_frame_bytes=252000, bytes_per_line=840
- stream[1] — interface 3, endpoint 0x82, bulk, Y8 (fourcc `Y8  `), 960×600, max_frame_bytes=576000, bytes_per_line=960
- stream[2] — interface 5, endpoint 0x83, bulk, YUY2, 1280×800, max_frame_bytes=2048000, bytes_per_line=2560
- interface 6 — HID interrupt 0x85, 248-byte reports, all-input; HID cannot carry laser/exposure control (#101 note printed by probe)
- probe note: IR/depth assignment needs the RPC→endpoint map (OQ-169e)

### --diag-interfaces (exit 0)

14 total IOUSBHostInterface nodes visible; 7 belong to scanner (vid=0x2BC5 pid=0x0C03). All 7 scanner interfaces report `owner="(none)"` — no kernel driver (including UVCAssistant) is attached to any scanner interface.

### --capture --stream depth (exit 2)

```
build/tools/raw_capture/raw_capture --capture --stream depth \
    --sink /tmp/uvc-depth.rcap --max-frames 30 --max-seconds 10
```

- Exit code: `2`
- Verdict: **FAIL**
- Raw log: `2026-10-24-plan-uvc-classic.raw.log` (same directory)

Output verbatim:
```
[capture] sink: /tmp/uvc-depth.rcap (16 bytes)
[capture] stop: source-error
[capture] detail: classic: could not create the device plug-in: 0xe00002be
[capture] frames: 0 (ir 0, depth 0, rgb 0), 0 record bytes
```

IR capture: not run (depth did not exit 0 with frames > 0).

sudo retry: not run — the task condition for a sudo retry is `kIOReturnExclusiveAccess` (0xe00002c5); the actual error is 0xe00002be (`kIOReturnNotPrivileged`), a distinct code, so the condition is not met.

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | --probe exits 0 and finds video-streaming interfaces | **[PASS]** exit 0, 3 streams enumerated |
| 2 | --diag-interfaces exits 0 and finds scanner interfaces | **[PASS]** exit 0, 7 scanner interfaces visible, all owner="(none)" |
| 3 | depth capture exits 0 with frames > 0 | **[FAIL]** exit 2; `classic: could not create the device plug-in: 0xe00002be` |
| 4 | --verify /tmp/uvc-depth.rcap exits 0 | **[SKIP]** not run — capture failed |
| 5 | IR capture exits 0 with frames > 0 | **[SKIP]** not run — depth capture failed |

## Crash sweep

No crash report for 'raw_capture' at/after 2026-10-24 05:00:54 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure — observable + one lead

- Observable: `build/tools/raw_capture/raw_capture --capture --stream depth --sink /tmp/uvc-depth.rcap --max-frames 30 --max-seconds 10`, exit 2; stderr: `[capture] detail: classic: could not create the device plug-in: 0xe00002be`; sink file /tmp/uvc-depth.rcap contains 16 bytes (header only, 0 frames).
- Lead: likely in the IOCreatePlugInInterfaceForService call in `tools/raw_capture/iokit_source.mm` — the process lacks the entitlement or privilege needed to open the device plug-in as a non-root user.

## Open questions

- Is 0xe00002be (`kIOReturnNotPrivileged`) from `IOCreatePlugInInterfaceForService` or from a subsequent `USBDeviceOpen`/`USBInterfaceOpenSeize` call?
- Why do all 7 scanner interfaces show `owner="(none)"` (no UVCAssistant claim) after a fresh power cycle — is this the expected post-bootout state, or does it indicate the device is in an unusual enumeration state?
- Would running as root produce a different error or succeed, and if it succeeds, what entitlement or privilege delegation path enables non-root access?
- The task's "if kIOReturnExclusiveAccess" sudo branch was not reached — does the fixer want a root-run result recorded as a separate run?

## Board

- Row move: #(uvc-classic) `HW` → `FAIL` — depth capture blocked at device plug-in creation (kIOReturnNotPrivileged, 0xe00002be); probe and diag-interfaces pass, capture does not.
