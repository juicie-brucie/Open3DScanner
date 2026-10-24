# PLAN #171/#178 — Depth + IR capture retest (combined find+seize retry) — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 02:03:44 >= newest source iokit_source.mm 02:03:36
- Interposer: expect first line: [launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib
- Run start: 2026-10-24 02:15:54

Device state: freshly power-cycled immediately before this run.
HEAD: cf21635 (raw_capture: retry the interface find+seize together, not just the seize)

RPCServer: `launchctl bootout gui/501/com.creality.RPCServer` → exit 3 (No such process); `pgrep -x RPCServer` → exit 1 (not running). Confirmed stopped.

Build: `cmake --build build --target raw_capture` → exit 0, "Built target raw_capture".

## Commands

```
build/tools/raw_capture/raw_capture --probe

build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-findseize/depth.bin

build/tools/raw_capture/raw_capture --capture --stream ir --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-findseize/ir.bin
```

## Probe result

Exit code: `0`

Device enumerated: vid=0x2BC5 pid=0x0C03, 7 interfaces (3 video-streaming + 1 video-control groups + 1 HID).
- stream[0]: interface 1, endpoint 0x81, bulk, Y8 480×300, max_frame_bytes=252000
- stream[1]: interface 3, endpoint 0x82, bulk, Y8 960×600, max_frame_bytes=576000
- stream[2]: interface 5, endpoint 0x83, bulk, YUY2 1280×800, max_frame_bytes=2048000

## Depth result

- Exit code: `2`
- Frames: 0
- Sink file: 16 bytes (header only, no frame data)
- Error: `[capture] stop: source-error` / `[capture] detail: interface 1 is not on the device (retried 15 times over ~1500ms)`

## IR result

- Exit code: `2`
- Frames: 0
- Sink file: 16 bytes (header only, no frame data)
- Error: `[capture] stop: source-error` / `[capture] detail: interface 3 is not on the device (retried 15 times over ~1500ms)`

## Comparison

Both streams fail identically: the combined find+seize retry exhausts 15 attempts (~1500 ms) and reports the interface not found. The error text and structure are identical for depth (interface 1) and IR (interface 3). --probe finds both interfaces at exit 0, but --capture cannot seize either.

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | RPCServer stopped before capture | **[PASS]** pgrep exit 1 |
| 2 | Build succeeds | **[PASS]** cmake exit 0 |
| 3 | --probe finds device and both interfaces | **[PASS]** exit 0, interface 1 and 3 listed |
| 4 | Depth capture: frames > 0, exit 0 | **[FAIL]** exit 2, 0 frames, "interface 1 is not on the device (retried 15 times over ~1500ms)" |
| 5 | IR capture: frames > 0, exit 0 | **[FAIL]** exit 2, 0 frames, "interface 3 is not on the device (retried 15 times over ~1500ms)" |

## Result

- Depth verdict: **FAIL**
- IR verdict: **FAIL**
- Overall verdict: **FAIL**
- Raw log: `2026-10-24-plan-uvc-findseize.raw.log` (same directory)
- Interposer confirmation: raw_capture does not create an OBScan context; interposer line not applicable

## Crash sweep

hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 02:15:54 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure or block — observable + one lead

- Observable: `build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 --sink ...` → exit 2, `[capture] detail: interface 1 is not on the device (retried 15 times over ~1500ms)`; same for IR with interface 3. --probe exit 0 finds both interfaces. Sink files 16 bytes (header only). Both streams fail identically.
- Lead: likely in the IOKit interface lookup used during --capture (the IOService matching that resolves interface numbers 1 and 3 is finding them at probe time but not at seize time; tools/raw_capture/iokit_source.mm)

## Open questions

- Why does --probe enumerate interface 1 and 3 successfully at exit 0, but --capture fails to find the same interfaces via the IOKit path used during seize, even after 15 retries?
- Does the UVCAssistant (pid 588) re-acquire exclusive ownership between the probe scan and the capture seize attempt, collapsing the service nodes visible to IOServiceGetMatchingService?
- Is the IOKit matching query used for the find+seize path the same as the one used for --probe, or does it use different matching criteria?
- Would a longer retry window (>1500 ms) or a different IOKit matching strategy succeed after a fresh power cycle?

## Board

- Row move: #171 `HW` → `HW` — combined find+seize retry still fails; both depth and IR interfaces unreachable at capture time despite probe success; fix insufficient, leave for builder
- Row move: #178 `HW` → `HW` — same as #171; IR-specific retest also fails identically
