# Crash analysis — `2026-10-17-plan8-device-message-listener.md`

**Date:** 2026-10-17
**Corrects:** [`2026-10-17-plan8-device-message-listener.md`](./2026-10-17-plan8-device-message-listener.md)
**Raised by:** operator, from the macOS crash report for that run.

The run doc is the hardware agent's as-observed record and is kept **unedited**,
per `tests/hardware/README.md:10` ("one file per run, never edited in place").
This file carries the analysis the record could not do for itself: it was written
from stdout alone, and the crash report that settles the question was never
collected.

## The run doc's conclusion is wrong

It concludes the trailing `SIGSEGV` was "very likely" the known, never-fixed
`libomp` crash in `obscan_context_destroy()`. That was an inference from a
*missing* log line — "no `DONE` line, no destroy line, so it must be the unlogged
teardown sequence". The crash report contradicts it
`[hw-contradicted: 2026-10-17-plan8-device-message-listener]`.

## What the crash report shows

Faulting stack (`.crash.ips`, most recent frame first):

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

- Exception: `EXC_BAD_ACCESS`, `SIGSEGV`, `KERN_INVALID_ADDRESS at 0x0` — a NULL
  dereference.
- Faulting thread id `14660873`, the same thread that prints the `GVCPClient.cpp`
  / `Ethernet.cpp NetDeviceWatcher` / `DeviceManager.cpp` lines in the raw log —
  the vendor's **NetDeviceWatcher (GVCP)** thread, not the worker's main thread
  (`14660842` throughout). **Correction (2026-10-17, reconcile):** this file
  originally called it the "polling device-watcher thread". That conflated two
  distinct watchers: `NetDeviceWatcher` is its own object, and the *polling*
  watcher (`LibusbDeviceWatcher`, whose `Create PollingDeviceWatcher!` printed
  on `14660842` at context-create — `raw.log:14`) never printed on `14660873`.
  Consistent with `crash.ips` frame 13,
  `libobsensor::pal::NetDeviceWatcher::start(…)::$_1`, and with PLAN #7's OQ-7d.
- The only images on the stack are `liborbbec_scan.3.5.3.dylib`,
  `libOrbbecSDK.1.7.1.dylib`, and `libsystem_pthread.dylib`. No `libomp`, no
  `texture_mode::shutdown_network()`, no `obscan_context_destroy()` frame.

The fault is therefore inside the vendor SDK's **device-connected callback**, on
the watcher thread, while delivering the `DEVICE_CONNECT` that this test's
listener registration caused. The crashing thread never ran the teardown calls,
so the run doc's reasoning about the teardown sequence cannot hold.

## Timing, from the raw log

Check 7 passes at `16:54:49.6`; the vendor logs `ObException.cpp:5 Trying to
create a device that you've already created!` for the *same* SN two lines later;
then a second `Pioneer device created!` burst; then the log stops mid-flush.

`[inferred]` The re-add after remove that check 7 exercised (the OQ-3 path) left
the vendor's device bookkeeping in a state where the callback's
`get_active_device()` dereferenced NULL — the "can the callback re-enter?" hazard
OQ-4 raised. Under that reading the crash is a consequence of exercising the
OQ-3 path, not of `obscan_context_destroy()`.

What this does **not** settle: whether the NULL is the callback re-entering while
the device object is re-created, or a pre-existing inconsistency the re-add
merely exposes. Separating those needs a controlled re-run or a look at
`device::OrbbecDevice::get_active_device()` — the latter is recon's, and
`.recon-cache/` holds the artifact. Filed as PLAN **#11**.

## Evidence archived for this run

| File | What it is |
| --- | --- |
| `2026-10-17-plan8-device-message-listener.raw.log` | the test's own stdout/stderr, verbatim (written by the run) |
| `2026-10-17-plan8-device-message-listener.crash.ips` | macOS crash report for the `SIGSEGV` (`~/Library/Logs/DiagnosticReports/device_message_listener_test-2026-10-17-165451.ips`) |
| `2026-10-17-plan8-device-message-listener.native-scan-log.txt` | the vendor SDK's own `scan_log` for this run (`/tmp/openscanner_ob_scan_log/scan_log_20261017-165446.txt`) |
| `2026-10-17-plan8-device-message-listener.sdk-log.txt` | the lower-level `ScannerStreamSDK.log` lines for this run only — that file is appended across runs, so the window was extracted rather than committing three days of history as one run |

The first was part of the original record; the last three were `/tmp` and
`~/Library` artefacts that would not have survived a reboot.

## Not the other two known failure modes

- **Not** the `obscan_context_destroy()`/libomp crash — no such frame on the
  stack, and the crash is on a different thread.
- **Not** the Vulkan/SuperPoint *hang* that `vulkan_disable_interpose.c` fixes
  (see `docs/research/live-scanning-pipeline-overview.md`) — the faulting stack
  never enters ncnn or Vulkan, and the process crashed in ~4 s instead of hanging.

## Consequence for the board

Every ABI-level check passed, so PLAN #8/#10 are `DONE`. But the run was not
clean, so "exercising the OQ-3 path is safe" is **not** established — #8's row
carries that caveat, and #11 exists so the gap is tracked rather than absorbed.
