# PLAN #171 — UVC GET_CUR baseline, depth only, fresh device — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 01:15:07 >= newest source iokit_source.mm 01:15:01
- Interposer: preflight expected first line `[launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib`; line was ABSENT from both --probe and --capture runs
- Run start: 2026-10-24 01:27:27
- Device state: freshly power-cycled immediately before this run

## Commands

```
launchctl bootout gui/501/com.creality.RPCServer   # exit 3 (not loaded — confirmed not running)
pgrep -x RPCServer                                  # exit 1 (empty — confirmed)
cmake --build build --target raw_capture            # exit 0 (up to date)
build/tools/raw_capture/raw_capture --probe         # exit 0
build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-getcur-baseline/depth.bin
```

## Result

- Exit code: `2`
- Verdict: **FAIL**
- Raw log: `2026-10-24-plan-uvc-getcur-baseline.raw.log` (same directory)
- Interposer confirmation: ABSENT from both runs; preflight indicates `daemon: n/a — the target does not create an OBScan context`

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | RPCServer stopped | **[PASS]** pgrep exit 1, no output |
| 2 | Binary freshness | **[PASS]** binary 01:15:07 >= newest source 01:15:01 |
| 3 | --probe succeeds | **[PASS]** exit 0; stream[0] on interface 1 (endpoint 0x81, bulk, 480x300 Y8) enumerated |
| 4 | GET_CUR(VS_PROBE_CONTROL) succeeds | **[FAIL]** not reached — error before negotiation: `interface 1 is not on the device` |
| 5 | Depth frames > 0 | **[FAIL]** frames: 0; sink file 16 bytes (header only, no frame data) |

Note: this does not match any of the three anticipated outcome cases. The capture failed before GET_CUR with a new interface-claim error not seen in prior runs (prior runs failed at `GET_CUR(VS_PROBE_CONTROL) failed: Unable to send device request`).

## Crash sweep

hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 01:27:27 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure — observable + one lead

- Observable: `build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 ...` exit 2; `[capture] stop: source-error`; `[capture] detail: interface 1 is not on the device`; sink file 16 bytes (header only); frames: 0. The --probe run immediately prior enumerated stream[0] on interface 1 successfully (exit 0).
- Lead: likely in the capture path's interface open/claim step — the tool enumerates interface 1 in --probe but cannot open or claim it in --capture (`tools/raw_capture/`).

## Open questions

- Why does --probe enumerate interface 1 successfully but --capture reports "interface 1 is not on the device" on the same fresh-power-cycle device?
- Is the --probe run itself claiming interface 1 (preventing --capture from claiming it), or does --capture use a different IOKit call path than --probe?
- Is the "interface 1 is not on the device" message from IOKit or from raw_capture's own mapping logic?
- Does this same failure occur if --probe is skipped and --capture is the first command run?

## Board

- Row move: #171 `HW` → `HW` — new failure mode (interface-claim error before GET_CUR) replaces prior GET_CUR failure; state-drift theory neither confirmed nor refuted; builder investigation needed
