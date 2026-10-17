# Hardware test — PLAN #8/#10: device_message_listener_test

**Date:** 2026-10-17
**Worktree/branch:** `Open3DScanner-wt-build1` / `build/plan8-interpose` (PR #34)
**Target program:** `tests/hardware/device_message_listener_test`
**Command:**
```
cmake --build build --target device_message_listener_test
./build/tests/hardware/device_message_listener_test/device_message_listener_test
```
**Device:** Creality Raptor Pro (CR0886100SN), USB3.0, connected and powered
**Exit code:** `1`
**Verdict:** **All seven explicit checks PASSED.** Exit code 1 comes from a
`SIGSEGV` **after** the last check had already passed — not a failed check, and
not the 180 s timeout (the whole run took ~4 seconds). Raw output:
`2026-10-17-plan8-device-message-listener.raw.log` (same directory).

> **Correction — applied later the same day, once the crash report was
> retrieved.** The sections below originally attributed that `SIGSEGV` to the
> known, never-fixed `libomp` crash in `obscan_context_destroy()` — an
> inference drawn from the *absence* of a `DONE`/destroy line in stdout. macOS
> had in fact written a crash report for this run
> (`2026-10-17-plan8-device-message-listener.crash.ips`, now archived here),
> and it **contradicts that attribution**: the fault is on the vendor's
> polling device-watcher thread, inside the device-connected callback, with no
> `libomp`, `shutdown_network`, or `obscan_context_destroy` frame on the stack.
> "The crash" is rewritten below against that evidence, and the earlier "not a
> new bug" conclusion is withdrawn
> `[hw-contradicted: 2026-10-17-plan8-device-message-listener]`.

## Evidence archived for this run

| File | What it is |
| --- | --- |
| `2026-10-17-plan8-device-message-listener.raw.log` | the test's own stdout/stderr, verbatim |
| `2026-10-17-plan8-device-message-listener.crash.ips` | macOS crash report for the `SIGSEGV` (`~/Library/Logs/DiagnosticReports/device_message_listener_test-2026-10-17-165451.ips`) |
| `2026-10-17-plan8-device-message-listener.native-scan-log.txt` | the vendor SDK's own `scan_log` for this run (`/tmp/openscanner_ob_scan_log/scan_log_20261017-165446.txt`) |
| `2026-10-17-plan8-device-message-listener.sdk-log.txt` | the lower-level `ScannerStreamSDK.log` lines for this run only — that file (`/tmp/openscanner_ob_sdk_log/`) is appended across runs, so the window was extracted rather than copying 3 days of history as one run |

The first two were written by the original run; the last two are `/tmp`
artefacts that would otherwise have been lost on reboot.

## Interposer check

The task asked to confirm the **first** line is `[launcher] DYLD_INSERT_LIBRARIES=
…libMoltenVK.dylib:…libvulkan_disable_interpose.dylib`. It is present with the
correct content —

```
[launcher] DYLD_INSERT_LIBRARIES=/Applications/CrealityScan.app/Contents/PlugIns/libMoltenVK.dylib:/Users/brucemackay/DevReposiMac/Open3DScanner-wt-build1/build/engine/libvulkan_disable_interpose.dylib
```

— pointing at this worktree's own freshly-built interpose dylib, so this was
unambiguously the right binary with the fix active. It is **not literally the
first printed line**, though: it's the launcher's own `printf`, made before the
`fork`/`exec`, but the launcher never `fflush`es it, so it sits in the parent's
stdio buffer while the worker child (which `fflush`es after every line) gets
its own output onto the shared stream first — the line lands near the *end* of
the capture (right before the crash report) purely from buffer-flush ordering
between the two processes, not because a different binary ran. Noted literally
per the instructions rather than silently waved off; worth an `fflush(stdout)`
right after that `printf` in the launcher if this check needs to be reliable
line-order in future runs, but that's a one-line source change and out of
scope here ("do not modify any source file").

## The seven checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `add(NULL handle, listener)` == `OB_SCAN_ERROR_DEADLINE_EXCEEDED` (845283591) | **PASS** |
| 2 | `add(handle, NULL listener)` == `OB_SCAN_ERROR_INVALID_ARGUMENT` (845283584) | **PASS** |
| 3 | `add(handle, listener_a)` #1 == `OB_SCAN_SUCCESS` | **PASS** |
| 4 | Double add (`listener_b`, no remove) == `OB_SCAN_ERROR_INVALID_ARGUMENT` (§2.3 single-slot claim) | **PASS** |
| 5 | `remove(handle, listener_a)` == `OB_SCAN_SUCCESS` | **PASS** |
| 6 | `remove(NULL handle, listener)` == `OB_SCAN_ERROR_DEADLINE_EXCEEDED` | **PASS** |
| 7 | `add(handle, listener_a)` after remove == `OB_SCAN_SUCCESS` (OQ-3: remove *does* clear the slot) | **PASS** |

`[worker] DONE (failures=...)` never printed — the process crashed after check
7 but before the summary line, so that count is inferred from the per-check
`[PASS]`/`[FAIL]` lines actually printed, all seven of which read `[PASS]`.

## OQ-4 — thread identity (resolved)

```
[worker]   messages delivered during add: 1; callback thread id hash=17304887900616587565; registration thread id hash=17304887900616587565
```

**The callback thread hash and the registration (calling) thread hash are
identical.** Combined with the already-established synchronous-delivery
finding (PLAN #1), this fully resolves OQ-4: the device-message callback on
registration fires **on the same thread that called
`add_device_msg_listener`**, not a separate vendor-internal thread — at least
for this specific delivery (the synchronous one fired during registration
itself; steady-state delivery during later `CONNECT`/`DISCONNECT` events from
the polling/GVCP watcher thread was not separately re-checked here since only
one `DEVICE_CONNECT` arrived, at `16:54:49`, and no thread-hash was logged for
it — only the registration-time message's thread was captured by the test's
`g_callback_thread`/`g_registration_thread` comparison).

## The crash

The test's stdout stops after check 7 and the process then died of `SIGSEGV`.
The original write-up had only that stdout and could not say where the fault
was; the crash report answers it directly.

Faulting stack (`2026-10-17-plan8-device-message-listener.crash.ips`, most
recent frame first, symbols as the vendor built them):

```
device::OrbbecDevice::get_active_device()
OBDeviceManagement::update_device_info(std::__1::list<device::DeviceIn…>)
OBDeviceManagement::update_camera_status(bool, device::DeviceConnectStatus…)
std::__1::__function::__func<OBDeviceManagement::bind_device_connect_callback…>
device::OrbbecInternalDevice::deviceConnectedCall(std::__1::shared_ptr…)
device::OrbbecInternalDevice::deviceChangedCallback(std::__1::shared_ptr…)
libobsensor::DeviceManager::onDeviceChanged(std::__1::vector<std::__1::s…>)
libobsensor::NetDeviceEnumerator::onPalDeviceChanged(libobsensor::pal…)
std::__1::__thread_proxy[abi:v15006]<…>
libsystem_pthread.dylib   _pthread_start
```

- Exception: `EXC_BAD_ACCESS`, `SIGSEGV`, `KERN_INVALID_ADDRESS at 0x0` — a
  NULL dereference.
- Triggered thread id is `14660873`. That is the same thread that prints the
  `GVCPClient.cpp` / `Ethernet.cpp NetDeviceWatcher` / `DeviceManager.cpp`
  lines in the raw log, i.e. the vendor's **polling device-watcher thread** —
  not the worker's main thread (which is `14660842` throughout).
- The only images on the stack are `liborbbec_scan.3.5.3.dylib`,
  `libOrbbecSDK.1.7.1.dylib`, and `libsystem_pthread.dylib`. There is no
  `libomp`, no `texture_mode::shutdown_network()`, and no
  `obscan_context_destroy()` frame.

So the fault is inside the vendor SDK's **device-connected callback**, on the
watcher thread, while delivering the `DEVICE_CONNECT` that this test's listener
registration caused — not in the worker's teardown sequence. The original
"no `DONE` line, no destroy line, so it must be the unlogged teardown calls"
reasoning was wrong: the crashing thread never ran the teardown calls at all.

Timing, from the raw log: check 7 passes at `16:54:49.6`; the vendor logs
`ObException.cpp:5 Trying to create a device that you've already created!` for
the *same* SN two lines later; then a second `Pioneer device created!` burst;
then the log stops mid-flush. `[inferred]` The re-add after remove that check 7
just exercised (the OQ-3 path) left the vendor's device bookkeeping in a state
where the callback's `get_active_device()` dereferenced NULL — which is exactly
the "can the callback re-enter?" hazard OQ-4 raised. Under that reading the
crash is a **consequence of exercising the OQ-3 path**, not of
`obscan_context_destroy()`.

What this does **not** settle: whether the NULL is the callback racing an
object's destruction, or a pre-existing inconsistency that the re-add merely
exposes. Distinguishing those needs either a controlled re-run (same sequence,
crash report captured) or a look at `device::OrbbecDevice::get_active_device()`
— the latter is a recon question, and `.recon-cache/` holds the artifact.

### Not the known `obscan_context_destroy` / libomp crash — attribution withdrawn

**This section originally concluded "very likely not a new bug," matching the
`SIGSEGV` to the crash below. That conclusion was wrong and is withdrawn.** It
argued from a missing log line — "no `DONE` line and no destroy line were
printed, so it must be the unlogged teardown sequence" — and the crash report,
retrieved afterwards, puts the fault on a different thread entirely, with no
`libomp`, `shutdown_network`, or `obscan_context_destroy` frame on the stack
(see "The crash" above)
`[hw-contradicted: 2026-10-17-plan8-device-message-listener]`. The interposer
was active and the process crashed in ~4 s rather than hanging, so neither the
libomp crash nor the Vulkan hang explains this run.

The *other* known crash is kept below as background, because it remains the
main documented way this SDK dies on macOS and the next run should watch for
it — but it does not explain this one. This project has a fully root-caused,
**never-fixed** `obscan_context_destroy()` crash on record, documented in
`README.md` ("Known issues", `obscan_context_destroy() crashes inside the
vendor SDK on macOS`): the call triggers `texture_mode::shutdown_network()`,
which constructs Creality's SuperPoint AI network (ncnn + Vulkan) purely to
immediately tear it down — the first OpenMP parallel region ever run in a
minimal host process, and the vendor's bundled `libomp.dylib` 5.0 segfaults on
a NULL `__kmp_threads` global during the join barrier, confirmed via lldb,
reproduced 100% of the time **on vendor-spawned worker threads**, not the
calling thread. Every direct fix was ruled out at the time (`NSApplicationLoad`,
`OMP_NUM_THREADS=1`, `KMP_DUPLICATE_LIB_OK=TRUE`, pre-warming `libomp`,
skipping the call entirely — which just trades it for a different crash, an
unjoined background thread aborting on exit). Its mitigation is the disposable
`RunSelfAsWorker` child process (`engine/include/openscanner/worker_process.h`),
which is also what kept this run's crash from taking the launcher down with it —
the launcher survived and reported the signal cleanly, exactly as designed.

For completeness, this is also **not** the Vulkan/SuperPoint *hang* that
`vulkan_disable_interpose.c` fixes (see
`docs/research/live-scanning-pipeline-overview.md`): the faulting stack never
enters ncnn or Vulkan, and the process crashed rather than hung.

The `ObException.cpp:5` **"Trying to create a device that you've already
created!"** warning is the one logged event that lines up with the crash
window — it appears for the same SN immediately after check 7's re-add
succeeded, i.e. re-registering the listener after remove appears to make the
vendor SDK attempt to *re-create* a device object that still exists. A
`get_active_device()` NULL deref on the watcher thread is consistent with
re-entering the connect callback during that re-creation, but the causal link
is `[inferred]`, not proven — it is left to the PLAN item filed for it, with
the `.ips` as the starting evidence.

## Recommendation for `/openscanner/reconcile`

- All of PLAN #8's and PLAN #10's own items (§2.3, OQ-3, §5 both error paths,
  OQ-4) are hardware-confirmed → `DONE`, **with one caveat**: the run was not
  clean. Every check passed and then the process died, so "the ABI surface
  behaves as specified" is confirmed while "exercising the OQ-3 path is safe"
  is not. Cite the OQ-3 result as `[hw-confirmed: …]` for the returned status
  code only.
- The `SIGSEGV` is **not** the documented `obscan_context_destroy()`/libomp
  crash and **not** the Vulkan hang — both retracted above on the crash
  report's evidence. It is a NULL deref inside the vendor's device-connected
  callback on the polling watcher thread, immediately after the OQ-3 re-add.
  File it as its own PLAN item citing this run, the `.ips`, and the
  `OrbbecDevice::get_active_device()` frame.
- Evidence for this run is archived next to this file: `.raw.log` (the test's
  stdout), `.crash.ips` (the macOS crash report), `.native-scan-log.txt` (the
  vendor's own `scan_log`), and `.sdk-log.txt` (the `ScannerStreamSDK.log`
  lines for this run's window only).
- One launcher nit, not a crash cause: the `[launcher] DYLD_INSERT_LIBRARIES=`
  line has no `fflush(stdout)` after it, so it is not reliably the first line
  of the capture. A one-line fix in the launcher; out of scope for a run whose
  instruction was "do not modify any source file".
- Not investigated further here per "do not modify any source file" — mapping
  the faulting frame is recon's.
