# Hardware test — device_message_listener_test

**Date:** 2026-10-17
**Worktree/branch:** `Open3DScanner-wt-hw` / `hw/next`
**Target program:** `tests/hardware/device_message_listener_test`
**Command:**
```
./build/tests/hardware/device_message_listener_test/device_message_listener_test
```
(run from `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`)

**Device:** Creality Raptor Pro (SN CR0886100SN), USB3.0, connected (per log output; not independently verified by this run beyond what the SDK reported)

**Exit code:** `1`

**Verdict:** **FAIL** (process did not exit cleanly — killed by `SIGSEGV` during
teardown, after all seven explicit `[PASS]`/`[FAIL]` checks printed `[PASS]`).
Raw output: `2026-10-17-device-message-listener-test.raw.log` (same directory).

This is not a timeout — the whole run completed (crashed) in under 3 seconds,
so rule 8 ("never retry a timeout") does not apply; no retry was attempted or
needed. No power-cycle of the device was performed or required.

## Launcher banner check (hard rule 6)

The required line is present in the captured output, with the expected
content:
```
[launcher] DYLD_INSERT_LIBRARIES=/Applications/CrealityScan.app/Contents/PlugIns/libMoltenVK.dylib:/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw/build/engine/libvulkan_disable_interpose.dylib
```
pointing at this worktree's own `libvulkan_disable_interpose.dylib`, so the
correct dylib chain was in effect.

**However, it is not literally the first line of the captured stream** — it
appears as the second-to-last line, immediately before the crash notice. The
very first line of output is `[vulkan_disable_interpose] loaded`, which is
itself only printed if the interpose dylib was actually loaded into the
process, so the correct injected library was in effect regardless of print
ordering. The banner's late appearance in the capture is consistent with a
known stdio-buffering effect reported in a prior run on this same test
(`2026-10-17-plan8-device-message-listener.md`, same repo): the launcher
prints the banner via `printf` before `fork`/`exec` but without `fflush`,
while the worker child flushes after every line, so the banner line lands
near the end of the merged stdout/stderr capture purely from buffer-flush
ordering between the two processes, not because a different binary ran. Noted
literally per the hard rule rather than silently waved off.

## The seven checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `add(NULL handle, listener)` == `OB_SCAN_ERROR_DEADLINE_EXCEEDED` (845283591) | **PASS** |
| 2 | `add(handle, NULL listener)` == `OB_SCAN_ERROR_INVALID_ARGUMENT` (845283584) | **PASS** |
| 3 | `add(handle, listener_a)` #1 == `OB_SCAN_SUCCESS` | **PASS** |
| 4 | Double add (`listener_b`, no remove) == `OB_SCAN_ERROR_INVALID_ARGUMENT` | **PASS** |
| 5 | `remove(handle, listener_a)` == `OB_SCAN_SUCCESS` | **PASS** |
| 6 | `remove(NULL handle, listener)` == `OB_SCAN_ERROR_DEADLINE_EXCEEDED` | **PASS** |
| 7 | `add(handle, listener_a)` after remove == `OB_SCAN_SUCCESS` (OQ-3: remove clears the slot) | **PASS** |

`[worker] DONE (failures=...)` never printed — the process crashed after
check 7 but before any summary line. All seven per-check lines that were
printed read `[PASS]`.

## The crash

Tail of the raw log:
```
[10/17 19:21:30.785572][warning][14835153][ObException.cpp:5] Trying to create a device that you've already created! SN/ID: CR0886100SN, uid: 30:30:34:30:35:30
[10/17 19:21:30.873165][info][14835163][GVCPClient.cpp:421] 0, 3, 248, 1 [**5 logs in 3005ms, last: 19:21:28.875057**]
[10/17 19:21:30.873182][info][14835164][GVCPClient.cpp:457] 2,1, 30:30:34:30:35:30,7, 5,3075, 127.0.0.1, 0.0.0.0, 0.0.0.0, Orbbec, Creality Raptor Pro, , Orbbec, CR0886100SN, #10010#USB3.0 [**5 logs in 3005ms, last: 19:21:28.875058**]
[launcher] DYLD_INSERT_LIBRARIES=/Applications/CrealityScan.app/Contents/PlugIns/libMoltenVK.dylib:/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw/build/engine/libvulkan_disable_interpose.dylib
worker did not exit cleanly: child killed by signal Segmentation fault: 11
```

No `obscan_context_destroy()` report line and no `DONE` line were ever
printed, so the crash occurred during the test's unlogged final teardown
sequence (the post-check-7 `remove` call and/or `obscan_context_destroy()`).
This matches the signature of an already-documented, never-fixed crash
recorded in a prior hardware run of this exact test on this worktree's sibling
build (`tests/results/2026-10-17-plan8-device-message-listener.md` and
`tests/results/2026-10-17-plan8-device-message-listener-crash-analysis.md`):
a `SIGSEGV` inside the vendor SDK's `obscan_context_destroy()` →
`texture_mode::shutdown_network()` path, attributed there to the bundled
`libomp.dylib` crashing on a NULL `__kmp_threads` global during an OpenMP
join barrier. This run's own crash was **not independently root-caused here**
(no debugger attached, no new analysis performed) — it is reported as
"not observed to differ from" that prior finding, not as confirmed identical.
The launcher survived and reported the signal cleanly, consistent with the
documented mitigation of running the risky call inside a disposable worker
process.

## Open questions

- Whether this run's segfault is the exact same root cause as the
  `libomp`/OpenMP one documented in the 2026-10-17 plan8 report, or a
  different fault at the same call site, was not verified in this run (no
  crash dump/backtrace was captured here, unlike the plan8 run which had a
  `.crash.ips` file). If a fresh root-cause check is wanted, a `.ips` crash
  report or debugger attach would be needed on a subsequent run.
- The launcher banner's non-first-line position is explained by a plausible
  buffering mechanism (seen identically in a prior run) but was not verified
  by this run beyond pattern-matching against that prior report.
