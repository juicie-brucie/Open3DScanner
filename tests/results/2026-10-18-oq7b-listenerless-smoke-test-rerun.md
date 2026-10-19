# 2026-10-18 — PLAN.md #62 (OQ-7b) rerun — listener-less with-device run

## Context

Rerun of PLAN.md row #62, following row #69's fix: `smoke_test`'s single
immediate `get_device_num()` poll was replaced with a 7-sample series at
500ms intervals, to stop racing the vendor watcher's ~1s detection latency
that confounded the first run
(`tests/results/2026-10-18-oq7b-listenerless-smoke-test.md`).

Question: does `obscan_device_management_get_device_num()` eventually return
nonzero with **no device-message listener ever registered**, once given
enough time past the watcher's detection latency?

## Setup checks

- Work tree: `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw` — confirmed
  via `pwd`.
- Branch: `hw/next` — confirmed via `git branch --show-current`.
- No competing SDK process: `ps aux | grep -iE "smoke_test|scan_server|device_message_listener"`
  returned no matches before the run.
- Binary freshness: `build/tools/smoke_test/smoke_test` mtime
  `1792369499` (2026-10-18 20:24) is newer than `tools/smoke_test/main.cpp`
  mtime `1792368640` (2026-10-18 20:10) — confirms the binary was rebuilt
  after the row #69 source edit.
- Read `tools/smoke_test/main.cpp` before running (lines 27, 88–137)
  to confirm the probe shape: 7 samples, 500ms apart, each printed as
  `[worker] get_device_num sample N/7 after Xms -> STATUS, count=C`, and to
  confirm the exit-code map in the file's header comment (lines 23–25):
  0 clean, 1 other launch failure, 2 skipped (no dylib), 3 recovered from the
  known `obscan_context_destroy()` vendor crash.
- Confirmed from source: `smoke_test` never sets `DYLD_INSERT_LIBRARIES`
  itself and has no Vulkan interposer/launcher banner by design (the
  worker-process isolation is its own mechanism, unrelated to the
  `[launcher] DYLD_INSERT_LIBRARIES=...` banner convention used by other
  harness-based tools). No README.md exists in `tools/smoke_test/`; the
  normal invocation, per `main.cpp`'s usage comment and the `argc > 1 ?
  argv[1] : FindDefaultLibraryPath()` fallback, is with no arguments.

## Command

```
./build/tools/smoke_test/smoke_test
```

Run from `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`, started
2026-10-18 20:26:45, finished 20:26:49 (wall clock, via `date` bracketing).

## Exit code

`3` — per the new map (row #69): **recovered from the known vendor
`obscan_context_destroy()` crash**. This is not "clean" (0) and not
"skipped" (2); it means the worker process was killed by the vendor SDK's
documented destroy-time SIGSEGV, and the launcher process itself exited
cleanly after detecting that.

Full stdout tail confirming this:
```
[launcher] Worker crashed inside the vendor SDK (child killed by signal Segmentation fault: 11). This is a known upstream issue (see README.md "Known issues") — the launcher process is unaffected and is exiting cleanly instead of propagating the crash.
[launcher] Exit code 3 = recovered from known vendor crash.
```
(shell-captured exit code, appended to the raw log: `EXIT:3`)

## Crash report

A macOS crash report was found for this run's time window:
`~/Library/Logs/DiagnosticReports/smoke_test-2026-10-18-202650.ips`
(timestamp 2026-10-18 20:26:50, matching the run's end at ~20:26:49–50).
Archived alongside this record as
`tests/results/2026-10-18-oq7b-listenerless-smoke-test-rerun.crash.ips`.
This matches the worker-process crash the launcher reported (signal 11,
inside the vendor SDK), consistent with the documented
`obscan_context_destroy()` defect — not a new/different crash.

## Device-count probe — all 7 samples

Raw log: `tests/results/2026-10-18-oq7b-listenerless-smoke-test-rerun.log`

| sample | nominal offset | wall-clock (approx, from interleaved vendor log) | status | count |
|---|---|---|---|---|
| 1/7 | 0ms   | 20:26:45.968 | OB_SCAN_SUCCESS | 0 |
| 2/7 | 500ms | 20:26:46.468 (between GVCP binds) | OB_SCAN_SUCCESS | 0 |
| 3/7 | 1000ms| 20:26:46.968 (just before watcher detection log) | OB_SCAN_SUCCESS | 0 |
| 4/7 | 1500ms| 20:26:47.468 (after watcher detection, before list update) | OB_SCAN_SUCCESS | 0 |
| 5/7 | 2000ms| 20:26:47.968 (~27ms before "Device changed" log) | OB_SCAN_SUCCESS | 0 |
| 6/7 | 2500ms| 20:26:48.468 (~473ms **after** vendor's own "Current device(s) list: (1)" log) | OB_SCAN_SUCCESS | 0 |
| 7/7 | 3000ms| 20:26:48.968 (~973ms after that same internal list update, just before `obscan_context_destroy()`) | OB_SCAN_SUCCESS | 0 |

Verbatim lines from the raw log:
```
[worker] get_device_num sample 1/7 after 0ms -> OB_SCAN_SUCCESS, count=0
[worker] get_device_num sample 2/7 after 500ms -> OB_SCAN_SUCCESS, count=0
[worker] get_device_num sample 3/7 after 1000ms -> OB_SCAN_SUCCESS, count=0
[worker] get_device_num sample 4/7 after 1500ms -> OB_SCAN_SUCCESS, count=0
[worker] get_device_num sample 5/7 after 2000ms -> OB_SCAN_SUCCESS, count=0
[worker] get_device_num sample 6/7 after 2500ms -> OB_SCAN_SUCCESS, count=0
[worker] get_device_num sample 7/7 after 3000ms -> OB_SCAN_SUCCESS, count=0
```

No sample came back nonzero.

Vendor's own interleaved log lines, confirming the device was detected and
internally tracked well before the probe window ended:
```
[10/18 20:26:46.991893][info][16336807][Ethernet.cpp:69] NetDeviceWatcher detected new device, uid:30:30:34:30:35:30
[10/18 20:26:47.995202][info][16336807][DeviceManager.cpp:188] Device changed! removed: 0, added: 1
[10/18 20:26:47.995217][info][16336807][DeviceManager.cpp:15] Current device(s) list: (1)
[10/18 20:26:47.995224][info][16336807][DeviceManager.cpp:24] 	- Name: CR-Scan Raptor Pro, PID: 0x0c03, SN/ID: CR0886100SN, Connection: USB3.0
```

## Checks

| check | result |
|---|---|
| Correct tree/branch confirmed before run | [PASS] |
| No competing SDK process confirmed | [PASS] |
| Binary rebuilt after source edit (mtime check) | [PASS] |
| Tool invoked with its normal (no-arg) invocation | [PASS] |
| `obscan_context_create()` succeeded | [PASS] |
| `obscan_context_get_device_management()` succeeded | [PASS] |
| All 7 device-count probe samples captured | [PASS] |
| Any sample returned nonzero count | [FAIL] (all 7 samples returned count=0) |
| Vendor-internal device detection/list-update observed in log | [PASS] (at ~991ms and ~2035ms respectively) |
| `get_device_num()` reflects vendor-internal list update within probe window | [FAIL] (still 0 at samples 6 and 7, ~473ms and ~973ms after the internal list update) |
| Crash report captured for the known destroy-time SIGSEGV | [PASS] (archived `.crash.ips`) |

## Verdict: FAIL (for the hypothesis that get_device_num() would go nonzero) — OQ-7b is now settled, not confounded

This run is **not confounded by the race** that undermined the first #62
run. The probe ran for 3 full seconds (7 samples at 500ms intervals), and
the vendor's own log shows the device was detected by the watcher at
~991ms and fully added to the internal device manager's list
(`DeviceManager.cpp:15 "Current device(s) list: (1)"`) at ~2035ms after
context creation — i.e., well inside the probe window, with two full
samples (6/7 and 7/7, at ~473ms and ~973ms after that internal update)
still reading `count=0` afterward.

**Reading: with no device-message listener ever registered, this single
hardware run shows `get_device_num()` staying at 0 even after the vendor
SDK's own logs confirm the device was detected and added to its internal
list.** That is evidence *against* "it eventually returns nonzero once you
wait past the watcher's latency" — the public count API did not reflect the
internally-tracked device at any of the 7 samples, spanning from before
detection to ~1s after the internal list was updated, in this one run.

This retires the *specific confound* from the first run (a single poll
racing ~1s watcher latency) — that criticism no longer applies, because
this probe spans 3 seconds and explicitly brackets the internal detection
event on both sides. It does **not** retire OQ-7b outright: this is a single
run on one device/firmware combination, and "stays 0 through sample 7" does
not by itself prove the count is *permanently* stuck for all elapsed time —
only that it is stuck for the ~1s past the internal list update that this
probe observed. A longer-duration probe (more samples, or a longer interval)
would be needed to rule out even-slower convergence. I recommend treating
OQ-7b as **answered for the window observed** (get_device_num() does not
track the internally-detected device within ~1s of listener-less operation)
but leaving the row open only if the board wants certainty about longer
horizons — my read is this is strong enough to retire the "does it ever
come up on its own, fast" framing of OQ-7b, with a residual note that
only a listener-driven callback (not polling get_device_num()) has been
shown to work on this SDK.

## Artifacts

- Raw log: `tests/results/2026-10-18-oq7b-listenerless-smoke-test-rerun.log`
- Crash report: `tests/results/2026-10-18-oq7b-listenerless-smoke-test-rerun.crash.ips`
- First run (superseded confound), for comparison: `tests/results/2026-10-18-oq7b-listenerless-smoke-test.md`
