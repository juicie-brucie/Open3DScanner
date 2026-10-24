# PLAN #171 / #178 — Depth + IR capture retest (hand-built matching dict) — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 02:24:33 >= newest source iokit_source.mm 02:24:21
- Interposer: expect first line: [launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib
- Run start: 2026-10-24 02:29:22

Device state: freshly power-cycled immediately before this run.

HEAD: fb14d95 (tests: land 2026-10-24 combined find+seize retest, depth+IR both unreachable (#171/#178))

RPCServer: `launchctl bootout gui/501/com.creality.RPCServer` → exit 3 (no such process). `pgrep -x RPCServer` → exit 1. Not running.

Build: `cmake --build build --target raw_capture` → exit 0 (already up to date, no recompile).

Binary freshness confirmed by preflight: binary 02:24:33 >= newest source iokit_source.mm 02:24:21.

## Probe

```
build/tools/raw_capture/raw_capture --probe
```

Exit 0. Three streams found:
- stream[0]: interface 1, endpoint 0x81, bulk, 480×300 Y8 (depth)
- stream[1]: interface 3, endpoint 0x82, bulk, 960×600 Y8 (IR)
- stream[2]: interface 5, endpoint 0x83, bulk, 1280×800 YUY2 (RGB)

## Depth capture

```
build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-handdict/depth.bin
```

- Exit code: `2`
- Verdict: **FAIL**

Output:
```
[capture] sink: tests/results/2026-10-24-plan-uvc-handdict/depth.bin (16 bytes)
[capture] stop: source-error
[capture] detail: interface 1 is not on the device (retried 15 times over ~1500ms)
[capture] frames: 0 (ir 0, depth 0, rgb 0), 0 record bytes
```

## IR capture

```
build/tools/raw_capture/raw_capture --capture --stream ir --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-handdict/ir.bin
```

- Exit code: `2`
- Verdict: **FAIL**

Output:
```
[capture] sink: tests/results/2026-10-24-plan-uvc-handdict/ir.bin (16 bytes)
[capture] stop: source-error
[capture] detail: interface 3 is not on the device (retried 15 times over ~1500ms)
[capture] frames: 0 (ir 0, depth 0, rgb 0), 0 record bytes
```

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | --probe enumerates device, exit 0 | **[PASS]** exit 0, streams[0–2] found |
| 2 | depth stream seizes interface 1, captures frames | **[FAIL]** `interface 1 is not on the device (retried 15 times over ~1500ms)` |
| 3 | IR stream seizes interface 3, captures frames | **[FAIL]** `interface 3 is not on the device (retried 15 times over ~1500ms)` |

Outcome case: **Same "is not on the device" error** — hand-built dictionary still not matching. Identical to prior run `2026-10-24-plan-uvc-findseize.md`.

## Crash sweep

No crash report for 'raw_capture' at/after 2026-10-24 02:29:22 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure — observable + one lead

- Observable: `build/tools/raw_capture/raw_capture --capture --stream depth ...` → exit 2, `interface 1 is not on the device (retried 15 times over ~1500ms)`; same for IR (interface 3). Both streams fail identically on a freshly power-cycled device after the hand-built `IOServiceMatching` + `CFDictionarySetValue` dictionary fix. `--probe` succeeds (exit 0, all three streams enumerated), so the device is reachable at the top level.
- Lead: likely in `FindInterfaceService()` or its equivalent in `tools/raw_capture/iokit_source.mm` — the dictionary keys or values being set for interface-level matching may differ from what IOKit publishes for these interface nodes (check whether `idVendor`/`idProduct`/`bInterfaceNumber` are the correct property names at the `IOUSBHostInterface` level, vs. `idVendorString`, `bInterfaceNumber` under a different registry path, or whether UVCAssistant has already claimed them before the open is attempted).

## Open questions

- Does `ioreg -p IOService` after a fresh power-cycle show the interface nodes (1 and 3) as already claimed by UVCAssistant at the moment `--capture` runs, and would that produce the same "not on the device" error text?
- Is the property name used for the interface number in the hand-built dictionary exactly the name IOKit uses for that class of node (could it be `bInterfaceNumber` vs. a different key)?
- The `--probe` path finds the device and enumerates interfaces successfully — which IOKit call path does it use, and does `--capture` use a different one for the seize?

## Board

- Row move: #171 `HW` → `HW` — depth stream still fails with identical "not on the device" error after hand-built dictionary fix; no change in behaviour
- Row move: #178 `HW` → `HW` — IR stream still fails with identical "not on the device" error after hand-built dictionary fix; no change in behaviour
