# PLAN #171/#178 — UVC interface-lookup direct-match retest, depth only, fresh device — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 01:36:34 >= newest source iokit_source.mm 01:36:25
- Interposer: expect first line: [launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib; ABSENT from both --probe and --capture runs (consistent with prior runs; target does not create an OBScan context)
- Run start: 2026-10-24 01:38:06
- HEAD: 1d50843
- Device state: freshly power-cycled immediately before this run

## Commands

```
launchctl bootout gui/501/com.creality.RPCServer   # exit 3 (not loaded — confirmed not running)
pgrep -x RPCServer                                  # exit 1 (empty — confirmed)
cmake --build build --target raw_capture            # exit 0 (up to date)
build/tools/raw_capture/raw_capture --probe         # exit 0
build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-direct-match/depth.bin
```

## Result

- Exit code: `2`
- Verdict: **FAIL**
- Raw log: `2026-10-24-plan-uvc-direct-match.raw.log` (same directory)
- Interposer confirmation: ABSENT from both runs (consistent with prior runs; target does not create an OBScan context)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | RPCServer stopped | **[PASS]** bootout exit 3 (not loaded); pgrep exit 1, no output |
| 2 | Binary freshness | **[PASS]** binary 01:36:34 >= newest source iokit_source.mm 01:36:25 (direct-match fix build) |
| 3 | --probe succeeds | **[PASS]** exit 0; stream[0] on interface 1 (endpoint 0x81, bulk, 480x300 Y8) enumerated |
| 4 | "interface 1 is not on the device" error gone | **[FAIL]** error still present: `interface 1 is not on the device (retried 10 times over ~500ms)` |
| 5 | Depth frames > 0 | **[FAIL]** frames: 0; sink file 16 bytes (header only, no frame data) |

Outcome case match: **"Same error, interface still not found"** — direct-match fix (createMatchingDictionaryWithVendorID:productID:...interfaceNumber: + IOServiceGetMatchingService) did not resolve the failure; interface 1 is not accessible via capture path even after the fix.

## Crash sweep

hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 01:38:06 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure — observable + one lead

- Observable: `build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 --sink ...` exit 2; `[capture] stop: source-error`; `[capture] detail: interface 1 is not on the device (retried 10 times over ~500ms)`; sink file 16 bytes; frames: 0. Direct-match IOKit lookup (same pattern used successfully for the top-level device) still cannot find interface 1 during --capture, even though --probe enumerates it successfully (exit 0) on the same binary immediately before.
- Lead: likely a permission or entitlement barrier preventing `IOServiceGetMatchingService` from returning the `IOUSBHostInterface` service at capture time, even when the device is visible — or the matching dictionary for the interface requires an additional key (`bInterfaceSubClass`, `bInterfaceProtocol`, or location-based) that the current implementation omits (`tools/raw_capture/iokit_source.mm`).

## Open questions

- Does the direct `IOServiceGetMatchingService` call return NULL or a valid-but-wrong service object for interface 1?
- Does the matching dictionary used in the fix include all required keys to uniquely identify interface 1, or could it be matching the wrong service (e.g. a stale or phantom entry)?
- Does running --capture without a preceding --probe (as the first command after power-cycle) produce a different result?
- Is there a macOS entitlement or IOKit permission (`com.apple.security.device.usb`) that `raw_capture` lacks, which would prevent `IOServiceGetMatchingService` from returning interface services while still allowing device-level enumeration used by --probe?
- Would logging the return value of `IOServiceGetMatchingService` (NULL vs. non-NULL) distinguish "not found" from "found but wrong"?

## Board

- Row move: #171/#178 `HW` → `HW` — direct-match fix does not resolve the failure; interface lookup approach needs further investigation
