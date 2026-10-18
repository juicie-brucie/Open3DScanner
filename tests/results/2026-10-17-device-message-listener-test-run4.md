# Hardware test — device_message_listener_test (run 4)

**Date:** 2026-10-17
**Worktree/branch:** `Open3DScanner-wt-hw` / `hw/board-row20`
**Target program:** `tests/hardware/device_message_listener_test`
**Purpose:** Controlled re-run (fourth run of this binary today), PLAN row #11 —
confirm/sharpen the crash signature and the row #20 main-thread-in-destroy
finding, with closer attention to event ordering around check 5.

**Command:**
```
./build/tests/hardware/device_message_listener_test/device_message_listener_test
```
(run from `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`, stdout+stderr
redirected to a log file; no `DYLD_INSERT_LIBRARIES` set manually — the
binary's own launcher sets it and forks the worker child, per
`tests/hardware/device_message_listener_test/CMakeLists.txt` / `main.cpp`'s
launcher-mode block). Binary present at
`build/tests/hardware/device_message_listener_test/device_message_listener_test`,
timestamp Oct 17 21:32 — unchanged since run 3, no rebuild performed.

**Pre-run check (hard rule 7):** `ps aux` immediately before this run showed
no `device_message_listener_test` and no other `orbbec`/GVCP-related process
attached. The two prior runs that day (`run2`, `run3`) had already exited
before this run started.

**Device:** Creality Raptor Pro (SN CR0886100SN), USB3.0 (per SDK log output;
not independently verified beyond what the SDK reported).

**Exit code:** `1`

**Verdict:** **FAIL** — all seven `[PASS]` checks printed, but the process
did not exit cleanly: killed by `SIGSEGV` during final teardown
(`obscan_context_destroy()`). Not a timeout — total wall time was ~3 s
(22:45:44 to 22:45:47), so hard rule 8 does not apply. No retry performed.
No power-cycle performed or required.

Raw output: `2026-10-17-device-message-listener-test-run4.raw.log` (same
directory). Crash report: `2026-10-17-device-message-listener-test-run4.crash.ips`
(copied from `~/Library/Logs/DiagnosticReports/device_message_listener_test-2026-10-17-224548.ips`,
timestamp matches the run window; the run's log tail is at 22:45:47.4 and the
report's `captureTime` is 22:45:47.6).

## Launcher banner check (hard rule 6)

Required line present (raw log line 83):
```
[launcher] DYLD_INSERT_LIBRARIES=/Applications/CrealityScan.app/Contents/PlugIns/libMoltenVK.dylib:/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw/build/engine/libvulkan_disable_interpose.dylib
```
pointing at this worktree's own `libvulkan_disable_interpose.dylib`. As in
every prior run, it is not the literal first line of the stream — it's
second-to-last, printed via an unflushed `printf` before fork/exec while the
worker child flushes every line. The actual first line,
`[vulkan_disable_interpose] loaded` (line 1), independently confirms the
interpose dylib was loaded into the running process, so the correct
binary/launcher is confirmed despite the print ordering.

## The seven checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `add(NULL handle, listener)` == `OB_SCAN_ERROR_DEADLINE_EXCEEDED` (845283591) | **PASS** |
| 2 | `add(handle, NULL listener)` == `OB_SCAN_ERROR_INVALID_ARGUMENT` (845283584) | **PASS** |
| 3 | `add(handle, listener_a)` #1 == `OB_SCAN_SUCCESS` | **PASS** |
| 4 | double `add(handle, listener_b)` (no remove) == `OB_SCAN_ERROR_INVALID_ARGUMENT` | **PASS** |
| 5a | `remove(handle, listener_a)` == `OB_SCAN_SUCCESS` | **PASS** |
| 5b | `remove(NULL handle, listener)` == `OB_SCAN_ERROR_DEADLINE_EXCEEDED` | **PASS** |
| 5c | `add(handle, listener_a)` after remove == `OB_SCAN_SUCCESS` (OQ-3 cleared slot) | **PASS** |

All seven `Check()` calls in `main.cpp` (lines 162, 167, 175, 185, 201, 206,
211) printed `[PASS]`. No `[FAIL]` observed. The process never reached the
`obscan_context_destroy() -> ...` print at `main.cpp:225` or the final
`DONE (failures=...)` print at `main.cpp:227` — confirming the crash happened
**after** all checks completed, inside the teardown sequence (the cleanup
`remove_fn(dm, &g_listener_a)` call at `main.cpp:218` and/or the
`obscan_context_destroy()` call at `main.cpp:224`), not during check 5 itself.

## Crash report

Faulting thread: **thread id 15062685** (the SDK's `NetDeviceWatcher` /
GVCPClient background thread — same thread ID that logged the GVCP traffic
throughout the run), `EXC_BAD_ACCESS (SIGSEGV)`, `KERN_INVALID_ADDRESS at
0x0000000000000000`.

Faulting frame stack (top down):
```
device::OrbbecDevice::get_active_device()                                          <- faulted here, far=0x0
OBDeviceManagement::update_device_info(std::list<device::DeviceInfo> const&)
OBDeviceManagement::update_camera_status(bool, DeviceConnectStatus, std::list<DeviceInfo>)
OBDeviceManagement::bind_device_connect_callback()::$_0 (lambda)
device::OrbbecInternalDevice::deviceConnectedCall(shared_ptr<ob::DeviceList>)
device::OrbbecInternalDevice::deviceChangedCallback(shared_ptr<ob::DeviceList>, shared_ptr<ob::DeviceList>)
device::OrbbecInternalDevice()::$_0 (lambda)
ob::Context::setDeviceChangedCallback(...)::$_0 (lambda)
libobsensor::DeviceManager::onDeviceChanged(vector<DeviceInfo>, vector<DeviceInfo>)
libobsensor::DeviceManager::DeviceManager(bool)::$_1 (lambda)
libobsensor::NetDeviceEnumerator::onPalDeviceChanged(OBDeviceChangedType, string)
libobsensor::NetDeviceEnumerator::NetDeviceEnumerator(...)::$_0 (lambda)
libobsensor::pal::NetDeviceWatcher::start(...)::$_1 (thread entry)
_pthread_start / thread_start
```

This is **identical in every frame** to the three prior archived crash
reports (`run2`, `run3`, and the original `plan8`/first run) — same
faulting symbol (`OrbbecDevice::get_active_device()`), same NULL deref
(`far=0`), same call chain through the vendor's connect-callback machinery,
same `NetDeviceWatcher`-owned thread.

**Main thread (thread id 15062671, `com.apple.main-thread`) at the same
instant:**
```
__psynch_mutexwait
_pthread_mutex_firstfit_lock_wait
_pthread_mutex_firstfit_lock_slow
std::__1::recursive_mutex::lock()
device::OrbbecInternalDevice::stop()
device::OrbbecInternalDevice::~OrbbecInternalDevice()
device::OrbbecDevice::~OrbbecDevice()
OBDeviceManagement::release()
obscan_context_destroy
(anonymous namespace)::RunTests(...) [inlined, main.cpp:224]
main [main.cpp:242]
start
```
The main thread was again concurrently **lock-waiting on a
`recursive_mutex`** inside `OrbbecInternalDevice::stop()`, reached via
`~OrbbecDevice()` → `OBDeviceManagement::release()` → `obscan_context_destroy`
— confirming the row #20 finding a fourth time, in a fourth independent run.

## Ordering evidence (raw log, timestamps from the SDK's own log lines)

| Event | Time / log line |
| --- | --- |
| (a) Test's `remove(handle, listener_a)` call returns `SUCCESS` | line 45–46, before 22:45:46.406 (no SDK timestamp on this worker-printed line, but it precedes line 49 in the stream) |
| (a cont.) Test's `remove(NULL handle, listener)` returns `DEADLINE_EXCEEDED` | line 47–48, same window |
| (b) Watcher thread's own `"Device changed! removed: 0, added: 1"` | line 49, **22:45:46.406462** |
| Device create (Pioneer device) begins/completes | lines 52–60, completes **22:45:46.729390** |
| (c) First device destroy (burst start) | line 61, **22:45:46.930564** |
| (c cont.) Second device re-create begins/completes | lines 67–75, completes **22:45:47.254168** |
| Test's `add(handle, listener_a) after remove` (check 5c) returns `SUCCESS` | line 77–78, no own timestamp, but after the 22:45:47.254 re-create and before the 22:45:47.318 warning below |
| **Second/later** "already created" warning — a further device-changed event racing an existing device | line 80, **22:45:47.318622**: `"Trying to create a device that you've already created! SN/ID: CR0886100SN"` |
| GVCP background log catch-up (aggregated, "[5 logs in 3005ms]") | lines 81–82, printed late relative to their own internal timestamps (~22:45:45.4) |
| Launcher banner (unflushed `printf`, printed late) | line 83 |
| `(d)` Fault / abnormal exit reported by the launcher | line 84: `"worker did not exit cleanly: child killed by signal Segmentation fault: 11"` — crash report `captureTime` **22:45:47.6057** |

Reading (d) against the check table: the crash is **not** during check 5
itself — all of check 5's three sub-assertions (5a/5b/5c) completed and
printed `[PASS]` before the crash. The create→destroy→create burst (c)
happens entirely between the watcher's own device-changed detection (b) and
the test's check-5c `add` call returning. The watcher's (b) event reports
`"removed: 0, added: 1"` — i.e. it is not reporting the *listener* removal
the test performed (that's an SDK-internal subscriber list, invisible to the
watcher); it looks like the device's own (re)discovery notification. By the
time the fault lands, there is a **second, later** device-changed-style
event in flight — evidenced by the `"already created"` warning at line 80,
which fires *after* check 5c already succeeded — and it is this later event,
racing the main thread's teardown call into `obscan_context_destroy()`, that
reaches the NULL deref in `get_active_device()`.

## Read on PLAN #11

This run sharpens, rather than overturns, the #15/#20 story:

- **Confirms #20 a fourth time:** the main thread is reliably inside
  `obscan_context_destroy()` → `OBDeviceManagement::release()` →
  `~OrbbecDevice` → `OrbbecInternalDevice::stop()`, blocked on a
  `recursive_mutex`, at the exact moment the watcher thread faults. Across
  four independent runs this is now a fully consistent pairing, not
  coincidence.
- **Sharpens #15's "remove races the watcher's own detection" framing:** in
  this run, check 5's `remove()`/`add()` calls all complete cleanly and the
  create→destroy→create burst resolves (device is successfully re-created)
  well before the crash. The crash instead coincides with a **second,
  independent** device-changed notification (the `"already created"`
  warning at 22:45:47.318, after check 5c's own success) that races the test
  harness's call into `obscan_context_destroy()`. This suggests the
  vulnerability is not specific to check 5's remove-then-add burst itself,
  but to *any* device-changed callback arriving on the watcher thread while
  the main thread is mid-teardown — check 5's burst is what reliably
  produces a second/trailing device-changed event close enough to teardown
  to race it, rather than being the direct cause of the race itself.
- The crash continues to occur strictly **after** all test logic completes
  (never mid-check), consistent with runs 2 and 3 and the original run —
  this is a teardown-time race in the vendor SDK, not a logic bug in the
  seven checks under test.

## Open questions

- Why does a device-changed callback keep firing well after the test's own
  `remove`/`add`/cleanup sequence (and after the device was already
  successfully re-created) — is the watcher thread still processing a queued
  notification from the earlier burst, or is this an independent
  periodic/keepalive re-announcement from the device itself?
- No source changes were made and none are proposed here, per hard rule 4 —
  this record is observation only.
