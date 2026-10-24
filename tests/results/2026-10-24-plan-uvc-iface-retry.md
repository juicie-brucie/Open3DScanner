# PLAN #171/#178 — UVC interface-lookup retry retest, depth only, fresh device — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 01:31:15 >= newest source iokit_source.mm 01:31:10
- Interposer: expect first line: [launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib; ABSENT from both --probe and --capture runs (consistent with prior run; target does not create an OBScan context)
- Run start: 2026-10-24 01:32:46
- Device state: freshly power-cycled immediately before this run

## Commands

```
launchctl bootout gui/501/com.creality.RPCServer   # exit 3 (not loaded — confirmed not running)
pgrep -x RPCServer                                  # exit 1 (empty — confirmed)
cmake --build build --target raw_capture            # exit 0 (up to date)
build/tools/raw_capture/raw_capture --probe         # exit 0
build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-iface-retry/depth.bin
```

## Result

- Exit code: `2`
- Verdict: **FAIL**
- Raw log: `2026-10-24-plan-uvc-iface-retry.raw.log` (same directory)
- Interposer confirmation: ABSENT from both runs (consistent with prior baseline run)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | RPCServer stopped | **[PASS]** bootout exit 3 (not loaded); pgrep exit 1, no output |
| 2 | Binary freshness | **[PASS]** binary 01:31:15 >= newest source iokit_source.mm 01:31:10 |
| 3 | --probe succeeds | **[PASS]** exit 0; stream[0] on interface 1 (endpoint 0x81, bulk, 480x300 Y8) enumerated |
| 4 | "interface 1 is not on the device" error gone | **[FAIL]** error still present with retry count: `interface 1 is not on the device (retried 10 times over ~500ms)` |
| 5 | Depth frames > 0 | **[FAIL]** frames: 0; sink file 16 bytes (header only, no frame data) |

Outcome case match: **"Same 'interface 1 is not on the device' error (now with retry count/duration in the text)"** — registry-race theory wrong, or unpublish window > 500ms.

## Crash sweep

hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 01:32:46 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure — observable + one lead

- Observable: `build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 --sink ...` exit 2; `[capture] stop: source-error`; `[capture] detail: interface 1 is not on the device (retried 10 times over ~500ms)`; sink file 16 bytes; frames: 0. Retry ran 10 times at 50ms intervals (~500ms total) and still could not find interface 1 in the IOKit registry. --probe enumerated interface 1 successfully (exit 0) immediately before.
- Lead: likely the interface child-lookup mechanism itself is wrong — the IOKit query used in `UsbInterfaceHandle::Open()` may not match how --probe's enumeration finds the same interface (`tools/raw_capture/`).

## Open questions

- Does the IOKit child-lookup in `UsbInterfaceHandle::Open()` use a different matching criterion than the enumeration path used by --probe?
- Is the "not on the device" error returned by IOKit itself (e.g. `kIOReturnNoDevice`) or by raw_capture's own mapping of a child-iterator result?
- Does the --probe path hold an open reference on the device or an interface, and could that reference prevent --capture's lookup from finding the child?
- Would skipping --probe (running --capture as the first command) produce a different result?
- Is the unpublish window for interface 1's IOKit child service consistently longer than 500ms, or is this a different failure mode from a transient race?

## Board

- Row move: #171/#178 `HW` → `HW` — retry fix confirmed not sufficient; interface lookup mechanism needs investigation, not just retry duration
