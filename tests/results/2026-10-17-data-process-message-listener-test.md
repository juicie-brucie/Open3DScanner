# Hardware test — data_process_message_listener_test

**Date:** 2026-10-17
**Worktree/branch:** `Open3DScanner-wt-hw` / `hw/next`
**Target program:** `tests/hardware/data_process_message_listener_test`

**Command:**
```
./build/tests/hardware/data_process_message_listener_test/data_process_message_listener_test
```
(run from `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`; output
redirected to a log file. `DYLD_INSERT_LIBRARIES` was **not** set manually —
the test binary's own built-in launcher forks a worker child and injects the
interpose dylib into it itself, per
`tests/hardware/data_process_message_listener_test/CMakeLists.txt`, which
wires `vulkan_disable_interpose` as a build dependency and passes its path in
via `VULKAN_DISABLE_INTERPOSE_DYLIB`.)

**Device:** Creality Raptor Pro / CR-Scan Raptor Pro (SN CR0886100SN),
USB3.0, connected (per SDK log output; not independently verified beyond
what the SDK reported).

**Exit code:** `0`

**Verdict:** **PASS** — all sixteen explicit `[PASS]` checks printed, the
worker reported `g_failures=0`, and the worker process exited cleanly with
code 0 (no crash, no timeout). Raw output:
`2026-10-17-data-process-message-listener-test.raw.log` (same directory).

The whole run completed in about 2 seconds. No retry was performed (hard
rule 8 n/a — not a timeout). No power-cycle of the device was performed or
required (hard rule 9 n/a).

## Launcher banner check (hard rule 6)

The required line is present in the captured output:
```
[launcher] DYLD_INSERT_LIBRARIES=/Applications/CrealityScan.app/Contents/PlugIns/libMoltenVK.dylib:/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw/build/engine/libvulkan_disable_interpose.dylib
```
pointing at this worktree's own `libvulkan_disable_interpose.dylib`,
confirming the correct dylib chain was in effect for the worker process.

As previously documented for `device_message_listener_test`
(`tests/results/2026-10-17-device-message-listener-test-run2.md`), this line
is **not literally the first line** of the captured stream — here it is the
second-to-last line, immediately before `worker exit code: 0`. The actual
first line is `[vulkan_disable_interpose] loaded`, which only prints if the
interpose dylib was in fact loaded into the process, so the correct injected
library was confirmed to be in effect despite the print-ordering quirk. This
is the same buffering-ordering pattern already noted for the sibling test
(launcher banner printed via unflushed `printf` before fork/exec; worker
child flushes every line as it goes) rather than evidence of a different
binary having run. Flagging this explicitly per hard rule 6 rather than
silently treating it as satisfied.

## The checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `add_data(NULL context)` == `OB_SCAN_ERROR_DEADLINE_EXCEEDED` | **PASS** |
| 2 | `add_process(NULL context)` == `OB_SCAN_ERROR_DEADLINE_EXCEEDED` | **PASS** |
| 3 | `add_data(NULL listener)` == `OB_SCAN_ERROR_INVALID_ARGUMENT` | **PASS** |
| 4 | `add_process(NULL listener)` == `OB_SCAN_ERROR_INVALID_ARGUMENT` | **PASS** |
| 5 | `add_data(listener_a)` #1 == `OB_SCAN_SUCCESS` | **PASS** |
| 6 | `add_process(listener_a)` #1 == `OB_SCAN_SUCCESS` | **PASS** |
| 7 | Double `add_data` (no remove) == `OB_SCAN_ERROR_INVALID_ARGUMENT` | **PASS** |
| 8 | Double `add_process` (no remove) == `OB_SCAN_ERROR_INVALID_ARGUMENT` | **PASS** |
| 9 | `remove_data(listener_a)` == `OB_SCAN_SUCCESS` | **PASS** |
| 10 | `remove_data` again (empty) == `OB_SCAN_ERROR_LISTENER_NOT_FOUND` | **PASS** |
| 11 | `remove_process(listener_a)` == `OB_SCAN_SUCCESS` | **PASS** |
| 12 | `remove_process` again (empty) == `OB_SCAN_ERROR_LISTENER_NOT_FOUND` | **PASS** |
| 13 | `add_data(listener_a)` after remove == `OB_SCAN_SUCCESS` | **PASS** |
| 14 | `add_process(listener_a)` after remove == `OB_SCAN_SUCCESS` | **PASS** |
| 15 | `remove_data(NULL context)` == `OB_SCAN_ERROR_DEADLINE_EXCEEDED` | **PASS** |
| 16 | `remove_process(NULL context)` == `OB_SCAN_ERROR_DEADLINE_EXCEEDED` | **PASS** |

Worker summary line: `[worker] g_failures=0`, followed by
`worker exit code: 0`.

## Device discovery observed in the log (informational, not a test check)

The SDK log shows the device being discovered mid-run via its GVCP
(network-emulated USB) client — consistent with the pattern seen in the
`device_message_listener_test` runs:
```
[info] NetDeviceWatcher detected new device, uid:30:30:34:30:35:30
[info] Device changed! removed: 0, added: 1
[info] 	- Name: CR-Scan Raptor Pro, PID: 0x0c03, SN/ID: CR0886100SN, Connection: USB3.0
```
This confirms the device was connected and enumerated during the run, but is
not itself one of the test's explicit checks.

## Comparison with the sibling test

Unlike `device_message_listener_test`, which has twice crashed with
`SIGSEGV` during teardown on this same build/device
(`tests/results/2026-10-17-device-message-listener-test.md`,
`tests/results/2026-10-17-device-message-listener-test-run2.md`,
`tests/results/2026-10-17-plan8-device-message-listener-crash-analysis.md`),
this run of `data_process_message_listener_test` exited cleanly with code 0
and no crash signature — `[worker] obscan_context_destroy() -> OB_SCAN_SUCCESS (0)`
was printed and the process returned normally. `[hw-confirmed: this run]`
this binary's teardown path did not reproduce the sibling test's SIGSEGV in
this single run.

## Open questions

- This is a single run (n=1). Per hard rule 8, no retry was performed and
  none is proposed. Whether `data_process_message_listener_test` is
  reliably crash-free across repeated runs was not established here — only
  that this one run completed cleanly.
