# Hardware test — data_process_message_listener_test (run 2)

**Date:** 2026-10-17
**Worktree/branch:** `Open3DScanner-wt-hw` / `hw/board-row20`
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
via `VULKAN_DISABLE_INTERPOSE_DYLIB` — mirroring `device_message_listener_test`'s
worker/interposer/180 s-timeout wiring, per PLAN.md row #19 (PR #53, `071438e`).)

**Device:** Creality Raptor Pro / CR-Scan Raptor Pro (SN CR0886100SN),
USB3.0, connected (per SDK log output; not independently verified beyond
what the SDK reported).

**Exit code:** `0`

**Verdict:** **PASS** — all sixteen explicit `[PASS]` checks printed, the
worker reported `g_failures=0`, `obscan_context_destroy() -> OB_SCAN_SUCCESS
(0)`, and the worker process exited cleanly with code 0 (no crash, no
timeout). Raw output:
`tests/results/2026-10-17-data-process-message-listener-test-run2.raw.log`
(same directory).

The whole run completed in about 2 seconds — far under the 180 s worker
timeout, so that budget was not exercised. No retry was performed (hard rule
8 n/a — not a timeout). No power-cycle of the device was performed or
required (hard rule 9 n/a).

## Launcher banner check (hard rule 6)

The required line is present in the captured output:
```
[launcher] DYLD_INSERT_LIBRARIES=/Applications/CrealityScan.app/Contents/PlugIns/libMoltenVK.dylib:/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw/build/engine/libvulkan_disable_interpose.dylib
```
pointing at this worktree's own `libvulkan_disable_interpose.dylib`,
confirming the correct dylib chain was in effect for the worker process.

As previously documented for this test
(`tests/results/2026-10-17-data-process-message-listener-test.md`) and for
`device_message_listener_test`
(`tests/results/2026-10-17-device-message-listener-test-run2.md`), this line
is **not literally the first line** of the captured stream — here it is
second-to-last, immediately before `worker exit code: 0` (raw log line 58 of
60). The actual first line is `[vulkan_disable_interpose] loaded`, which only
prints if the interpose dylib was in fact loaded into the process, so the
correct injected library was confirmed to be in effect despite the
print-ordering quirk (unflushed `printf` launcher banner vs. the worker
child's line-flushed output). Flagging this explicitly per hard rule 6 rather
than silently treating it as satisfied.

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
`[worker] obscan_context_destroy() -> OB_SCAN_SUCCESS (0)` and
`worker exit code: 0`.

## Device discovery observed in the log (informational, not a test check)

The SDK log shows the device being discovered mid-run via its GVCP
(network-emulated USB) client, same pattern as the prior run:
```
[info] NetDeviceWatcher detected new device, uid:30:30:34:30:35:30
[info] Device changed! removed: 0, added: 1
[info] 	- Name: CR-Scan Raptor Pro, PID: 0x0c03, SN/ID: CR0886100SN, Connection: USB3.0
```
This confirms the device was connected and enumerated during the run, but is
not itself one of the test's explicit checks. Timestamps: the sixteen checks
(raw log lines 18-33) are all printed by the worker immediately after
`obscan_context_create()` at `22:37:59.084`-ish, with no SDK log lines
interleaved, so they ran and completed before the GVCP watcher found the
device (`NetDeviceWatcher detected new device`, line 43, `22:38:00.101324`).
The device was then added to the device list (line 45, `22:38:01.106469`)
about one millisecond before `Context destroyed` printed (line 48,
`22:38:01.107125`). So in this run the add/remove checks exercised an empty
device list throughout, and the device's arrival landed right at (around,
not demonstrably before or after within this log's resolution) the
`obscan_context_destroy()` boundary. This differs from the prior run's log
shape only in timing detail, not in outcome.

## Crash-report check (hard rule 11)

`~/Library/Logs/DiagnosticReports/` was checked after the run. The newest
entries there are three `device_message_listener_test-2026-10-17-*.ips`
files from earlier today (20:01, 19:21, 16:54) and one from 2026-10-16 —
none in this run's window (started and finished within
`22:37:59`–`22:38:01`). No crash report was generated for this run, which is
consistent with the clean exit code 0 and the absence of any
signal/non-clean-exit message in the raw log.

## Comparison with prior runs

This reproduces the first run's clean result
(`tests/results/2026-10-17-data-process-message-listener-test.md`, also
exit 0, 16/16 `[PASS]`) on a different branch checkout
(`hw/board-row20` vs. that run's `hw/next`) of the same worktree and build.
Unlike `device_message_listener_test`, which has repeatedly crashed with
`SIGSEGV` during teardown on this same device
(`tests/results/2026-10-17-device-message-listener-test.md`,
`-run2.md`, `2026-10-17-plan8-device-message-listener-crash-analysis.md`),
both runs of `data_process_message_listener_test` have now exited cleanly
with no crash signature. `[hw-confirmed: this run]` adds a second
clean-exit data point to `[hw-confirmed: 2026-10-17-data-process-message-listener-test.md]`.

## Open questions

- This test's explicit checks (add/remove return codes, double-add/remove
  semantics, NULL-argument handling) are now confirmed clean across two runs.
  It does **not** exercise OQ-P1…P4 (data = sync drop-at-emit timing,
  process = thread-pool async enqueue timing/ordering, the exact six
  process-message ids, and `judge_is_exist_listener`'s "both slots
  mandatory before preview" gate) — this test never calls
  `obscan_scan_preview` or inspects delivered message payloads/timing, so
  **OQ-P1…P4 remain open** after this run, same as before. Per hard rule 8,
  no retry was performed and none is proposed; whether the test is reliably
  crash-free beyond n=2 was not established here.
