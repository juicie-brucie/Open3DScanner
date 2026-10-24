# PLAN #171/#178 — Depth-only capture retest after DeviceSeize fix — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 01:55:44 >= newest source iokit_source.mm 01:55:38
- Interposer: expect first line: [launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib
- Run start: 2026-10-24 01:58:55

Device note: scanner freshly power-cycled immediately before this run.

RPCServer state: `launchctl bootout gui/501/com.creality.RPCServer` → "Boot-out failed: 3: No such process"; `pgrep -x RPCServer` → exit 1 (not running). RPCServer was already absent; no action required.

sudo: not used.

## Commands

```
cmake --build build --target raw_capture

build/tools/raw_capture/raw_capture --probe

build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-seize/depth.bin
```

## --probe summary

Exit 0. Three video-streaming interfaces found:
- stream[0]: interface 1, endpoint 0x81, bulk, 480×300 Y8 (depth candidate, max_frame_bytes=252000)
- stream[1]: interface 3, endpoint 0x82, bulk, 960×600 Y8
- stream[2]: interface 5, endpoint 0x83, bulk, 1280×800 YUY2

## Capture result

```
[capture] sink: tests/results/2026-10-24-plan-uvc-seize/depth.bin (16 bytes)
[capture] stop: source-error
[capture] detail: interface 1 is not on the device
[capture] frames: 0 (ir 0, depth 0, rgb 0), 0 record bytes
exit: 2
```

- Exit code: `2`
- Sink file size: 16 bytes (header only, no frames)
- Verdict: **FAIL**
- Raw log: `2026-10-24-plan-uvc-seize.raw.log` (same directory)
- Interposer confirmation: launcher line not present — raw_capture does not use the Vulkan launcher path; preflight confirmed interposer is not expected for this target

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | exit 0 | **[FAIL]** exit 2 |
| 2 | frames > 0 | **[FAIL]** 0 frames |
| 3 | sink ~252000 B | **[FAIL]** 16 bytes (header only) |
| 4 | seize succeeds / no source-error | **[FAIL]** `[capture] detail: interface 1 is not on the device` |

## Crash sweep

no crash report for 'raw_capture' at/after 2026-10-24 01:58:55 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure or block — observable + one lead

- Observable: `build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 --sink ...` → exit 2; `[capture] stop: source-error`; `[capture] detail: interface 1 is not on the device`; sink file 16 bytes. The error occurs before any seize attempt is reported — no seize log line appeared.
- Lead: likely in the interface-lookup / open path in `tools/raw_capture/iokit_source.mm` — "interface 1 is not on the device" suggests the IOUSBHostInterface for interface number 1 is not being found under the device node at open time (possibly UVCAssistant has restructured the IOService tree, hiding the interface from the lookup used by the seize path).

## Open questions

- Does `ioreg -p IOService -n "Creality Raptor Pro" -l -r` still show `UsbExclusiveOwner = "pid ..., UVCAssistant"` on interface 1 after a fresh power-cycle with no RPCServer running?
- Is the interface-lookup in the seize path searching by interface number on the device node, or walking the full IOService tree? Could UVCAssistant's claim cause the interface node to appear under a different parent (e.g. an IOUSBHostDevice child vs. sibling)?
- Did the seize attempt execute at all, or did the failure occur before the seize call — i.e., is "interface 1 is not on the device" a lookup failure, not a seize rejection?

## Board

- Row move: #171/#178 `HW` → `HW` — FAIL; seize-path lookup returns "interface 1 is not on the device" before any seize attempt; needs builder investigation of interface-lookup logic in iokit_source.mm
