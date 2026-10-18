# Hardware test — device_message_listener_test (run 2, same day)

**Date:** 2026-10-17
**Worktree/branch:** `Open3DScanner-wt-hw` / `hw/next`
**Target program:** `tests/hardware/device_message_listener_test`
**Command:**
```
./build/tests/hardware/device_message_listener_test/device_message_listener_test
```
(run from `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`; output redirected
to a log file, no `DYLD_INSERT_LIBRARIES` was set manually — the test binary's
own built-in launcher, which forks a worker child and injects the interpose
dylib into it, handled that per `tests/hardware/device_message_listener_test/CMakeLists.txt`)

**Device:** Creality Raptor Pro (SN CR0886100SN), USB3.0, connected (per SDK log
output; not independently verified beyond what the SDK reported)

**Exit code:** `1`

**Verdict:** **FAIL** — process did not exit cleanly; killed by `SIGSEGV`
during teardown, after all seven explicit `[PASS]`/`[FAIL]` checks printed
`[PASS]`. Raw output: `2026-10-17-device-message-listener-test-run2.raw.log`
(same directory).

This was not a timeout — the whole run (including the crash) completed in
about 3 seconds, so hard rule 8 ("never retry a timeout") does not apply. No
retry was performed; this is a single run. No power-cycle of the device was
performed or required.

## Launcher banner check (hard rule 6)

The required line is present in the captured output:
```
[launcher] DYLD_INSERT_LIBRARIES=/Applications/CrealityScan.app/Contents/PlugIns/libMoltenVK.dylib:/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw/build/engine/libvulkan_disable_interpose.dylib
```
pointing at this worktree's own `libvulkan_disable_interpose.dylib`, confirming
the correct dylib chain was in effect for the worker process.

As in the prior same-day run (`tests/results/2026-10-17-device-message-listener-test.md`),
this line is **not literally the first line** of the captured stream — it is
the second-to-last line, immediately before the crash notice. The actual
first line is `[vulkan_disable_interpose] loaded`, which only prints if the
interpose dylib was in fact loaded into the process, so the correct injected
library was confirmed to be in effect despite the print ordering. This
reproduces the same buffering-ordering pattern noted in that prior report
(launcher banner printed via unflushed `printf` before fork/exec, worker
child flushes every line) rather than indicating a different binary ran.

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
check 7 but before any summary line, same as the earlier run today.

## The crash

Tail of the raw log:
```
[10/17 20:01:46.877381][warning][14882533][ObException.cpp:5] Trying to create a device that you've already created! SN/ID: CR0886100SN, uid: 30:30:34:30:35:30
[10/17 20:01:46.971887][info][14882542][GVCPClient.cpp:457] 2,1, 30:30:34:30:35:30,7, 5,3075, 127.0.0.1, 0.0.0.0, 0.0.0.0, Orbbec, Creality Raptor Pro, , Orbbec, CR0886100SN, #10010#USB3.0 [**5 logs in 3006ms, last: 20:01:44.974071**]
[10/17 20:01:46.972188][info][14882541][GVCPClient.cpp:421] 0, 3, 248, 1 [**5 logs in 3006ms, last: 20:01:44.974069**]
[launcher] DYLD_INSERT_LIBRARIES=/Applications/CrealityScan.app/Contents/PlugIns/libMoltenVK.dylib:/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw/build/engine/libvulkan_disable_interpose.dylib
worker did not exit cleanly: child killed by signal Segmentation fault: 11
```

No `obscan_context_destroy()` report line and no `DONE` line were printed, so
the crash occurred during the test's unlogged final teardown sequence (the
post-check-7 `remove` call and/or `obscan_context_destroy()`), same signature
as the earlier same-day run and the previously-documented crash in
`tests/results/2026-10-17-plan8-device-message-listener.md` /
`tests/results/2026-10-17-plan8-device-message-listener-crash-analysis.md`
(attributed there to a `SIGSEGV` inside `obscan_context_destroy()` →
`texture_mode::shutdown_network()`, itself attributed to `libomp.dylib`
crashing on a NULL `__kmp_threads` global during an OpenMP join barrier).
This run's crash was **not independently root-caused here** (no debugger
attached, no crash dump captured) — reported only as "not observed to differ
from" the prior findings, now reproduced for the second time today on this
exact binary and worktree, which strengthens (but does not newly prove) the
determinism of the teardown crash.

## Open questions

- Same open question as the prior same-day report: whether this is the exact
  same root cause as the `libomp`/OpenMP fault documented in the plan8 report,
  or a different fault at the same call site — not verified in this run (no
  `.ips` crash report or debugger attach was captured here).
- This is now the second reproduction of the identical crash signature today
  on this build; it has not regressed or changed, but has also not been fixed.
