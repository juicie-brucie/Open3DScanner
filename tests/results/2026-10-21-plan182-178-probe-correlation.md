# PLAN #182/#178 — raw_capture probe/table/correlation — PASS

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 18:21:12 >= newest source correlation.cpp 18:21:07
- Interposer: expect first line: [launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib
- Run start: 2026-10-21 18:35:58
- Device serial: CR0886100SN
- HEAD SHA: 34d11e61aff07b7d206e7fb0641c686341fb236b

Three sub-commands run in sequence: `--table`, `--probe --match`, `--probe --watch --seconds 10`.

## Commands

```
build/tools/raw_capture/raw_capture --table
build/tools/raw_capture/raw_capture --probe --match
build/tools/raw_capture/raw_capture --probe --watch --seconds 10
```

## Result

- Exit code: `0` (all three sub-commands)
- Verdict: **PASS**
- Raw log: `2026-10-21-plan182-178-probe-correlation.raw.log` (same directory)
- Interposer confirmation: interposer line expected but not confirmed in this output — raw_capture --table/--probe do not load the vendor SDK path; the launcher line was not present in output.

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | --table prints descriptor table with known VID/PID | **[PASS]** `[table] identity: VID 0x2BC5 PID 0x0C03 (known)` |
| 2 | --table identifies RPC ports for IR, depth, RGB, IMU | **[PASS]** IR 10013, depth 10010, RGB 10012, IMU 10011 — all marked (known) |
| 3 | --table usb endpoint and format/stride fields | **[FAIL — not yet mapped]** both fields `unknown` for IR and depth |
| 4 | --probe --match correlates VS interfaces to streams | **[PASS]** depth→interface 1, IR→interface 3, RGB→interface 5, HID→interface 6 |
| 5 | --probe --match shows active alternate setting > 0 | **[FAIL — not observed]** all interfaces at alt=0; note: `no stream was active at this sample, or the registry does not track SET_INTERFACE` |
| 6 | --probe --watch 10 s shows any alt-setting change | **[FAIL — not observed]** all 7 interfaces remained at alt=0 throughout 10-second watch |
| 7 | --probe --match control interface identified | **[FAIL — not mapped]** `control (laser/exposure): unknown — HID report descriptor and vendor control endpoint are not in the map` |

## Crash sweep

hw-postflight: no crash report for 'raw_capture' at/after 2026-10-21 18:35:58 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure — observable + one lead

- Observable: `--probe --match` and `--probe --watch --seconds 10` both report every VideoStreaming interface at alternate_setting=0 throughout; note text: `no stream was active at this sample, or the registry does not track SET_INTERFACE`. USB endpoint and format/stride fields remain `unknown` in `--table`.
- Lead: likely the macOS IOKit USB registry does not reflect SET_INTERFACE changes made by the vendor RPC daemon — descriptor_table.h has no USB endpoint entries for IR/depth streams yet.

## Open questions

- Does macOS IOKit track alternate-setting changes via SET_INTERFACE at all, or does the tool need a different observation method?
- Are the vendor RPC streams (ports 10010/10013) actually issuing SET_INTERFACE on the USB bus, or do they use a different isochronous-start mechanism?
- What USB endpoint numbers and isochronous packet sizes does the depth stream (interface 1) and IR stream (interface 3) use?

## Board

- Row move: #182 `HW` → `HW` — --table and --match run cleanly; interface correlation established (depth=1, IR=3, RGB=5, HID=6); alt-setting tracking not yet functional — USB endpoint fields still unknown
- Row move: #178 `HW` → `HW` — no alt-setting change observed in 10 s watch with RPCServer running; all VS interfaces stayed at alt=0
