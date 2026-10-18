# Hardware test — device_message_listener_test (run 3)

**Date:** 2026-10-17
**Worktree/branch:** `Open3DScanner-wt-hw` / `hw/board-row20`
**Target program:** `tests/hardware/device_message_listener_test`
**Purpose:** PLAN row #17 (OQ-4) — per-phase delivery-thread identity, another
data point for the async/watcher-thread half.

**Command:**
```
./build/tests/hardware/device_message_listener_test/device_message_listener_test
```
(run from `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`, stdout+stderr
redirected to a log file; no `DYLD_INSERT_LIBRARIES` set manually — the test
binary's own launcher sets it and forks the worker child, per
`tests/hardware/device_message_listener_test/CMakeLists.txt` /
`main.cpp`'s launcher-mode block). Binary present at
`build/tests/hardware/device_message_listener_test/device_message_listener_test`,
built 2026-10-17 21:32, prior to this run.

**Pre-run check (hard rule 7):** `ps aux` showed no `device_message_listener_test`,
no other `obscan`/`GVCP`-related process attached before this run started; the
background process visible in `ps aux` was an unrelated verifier/PR-review
shell job, not an SDK user.

**Device:** Creality Raptor Pro (SN CR0886100SN), USB3.0 (per SDK log output;
not independently verified beyond what the SDK reported).

**Exit code:** `1`

**Verdict:** **FAIL** — all seven `[PASS]`/`[FAIL]` checks printed `[PASS]`,
but the process did not exit cleanly: killed by `SIGSEGV` during teardown.
Not a timeout — total wall time was ~4 s (22:41:06 to 22:41:10), so hard rule
8 does not apply. No retry performed. No power-cycle performed or required.

Raw output: `2026-10-17-device-message-listener-test-run3.raw.log` (same
directory). Crash report: `2026-10-17-device-message-listener-test-run3.crash.ips`
(copied from `~/Library/Logs/DiagnosticReports/device_message_listener_test-2026-10-17-224111.ips`,
timestamp matches the run window).

## Launcher banner check (hard rule 6)

Required line present in the captured output (raw log line 83):
```
[launcher] DYLD_INSERT_LIBRARIES=/Applications/CrealityScan.app/Contents/PlugIns/libMoltenVK.dylib:/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw/build/engine/libvulkan_disable_interpose.dylib
```
pointing at this worktree's own `libvulkan_disable_interpose.dylib`. As in
every prior run of this binary, the line is not literally the first line of
the stream (it's second-to-last, printed via an unflushed `printf` before
fork/exec while the worker child flushes every line) — the actual first line,
`[vulkan_disable_interpose] loaded`, confirms the interpose dylib was in fact
loaded into the running process, so the correct binary/launcher is confirmed
despite the print ordering.

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

`[worker] DONE (failures=...)` never printed; the process crashed after check
7's associated callback activity but before the destroy call's own print line.

## Per-phase thread-hash breakdown (OQ-4)

The test hashes `std::this_thread::get_id()` with `std::hash<std::thread::id>`
both for the registration thread (captured once, right before the first
`add`) and for every thread that runs `OnDeviceMessage`. Raw values from this
run:

- Registration thread hash: `17304887900616587565`

| Phase | Messages delivered | Callback thread hash(es) | Same as registration thread? |
| --- | --- | --- | --- |
| After `add` #1 (check 3) | 1 | `17304887900616587565` | **Yes** |
| After the 2 s wait (no new messages arrived during the wait itself; cumulative total still 1) | 1 (cumulative) | `17304887900616587565` | **Yes** |
| After remove-then-re-add (check 7) | 1 | `17304887900616587565` | **Yes** |

All three observed deliveries to the test's registered `OnDeviceMessage`
landed on the single thread that also ran the test's main sequence
(registration, the `add`/`remove` calls) — i.e. every observed delivery, in
this run, was synchronous/on-registration-thread. Same pattern as the run
this task cites as the prior data point ("both observed deliveries landed on
the registration thread").

The one message observed per phase was a `DEVICE_CONNECT`/`DEVICE_DISCONNECT`
pair triggered by the device's own USB (re)enumeration during the test, not
spontaneous async traffic arriving during the idle 2 s wait — the raw log
shows the second delivery (`DEVICE_CONNECT`, line 76) landing well after the
2 s-wait print (line 32) and its own `after 2s wait` report (line 44), i.e.
*after* the remove/re-add sequence started, not during the wait itself. So
this run, like the cited prior one, still does not exercise a case where a
message arrives spontaneously, independent of the test's own `add`/`remove`
calls, while the main thread is idle.

## The crash — new information this run adds

**This crash signature is not new** — it reproduces the one already
root-caused in `tests/results/2026-10-17-plan8-device-message-listener-crash-analysis.md`
(which itself corrects an earlier, wrong "libomp" attribution in the original
`plan8` record). Root-caused here again independently from this run's own
`.crash.ips`:

- Exception: `EXC_BAD_ACCESS` / `SIGSEGV` / `KERN_INVALID_ADDRESS at 0x0` — a
  NULL dereference.
- Faulting thread: OS tid `15056758` — the thread that prints every
  `GVCPClient.cpp` line in the raw log, and whose bottom-of-stack frame is
  `libobsensor::pal::NetDeviceWatcher::start(...)::$_1` → `__thread_proxy` →
  `_pthread_start`/`thread_start`. This is the vendor's **NetDeviceWatcher**
  background thread, distinct from the main/registration thread (OS tid
  `15056739` throughout the raw log).
- Faulting stack (top first):
  ```
  device::OrbbecDevice::get_active_device()
  OBDeviceManagement::update_device_info(...)
  OBDeviceManagement::update_camera_status(...)
  std::__1::__function::__func<OBDeviceManagement::bind_device_connect_callback()::$_0, ...>::operator()(...)
  device::OrbbecInternalDevice::deviceConnectedCall(...)
  device::OrbbecInternalDevice::deviceChangedCallback(...)
  std::__1::__function::__func<device::OrbbecInternalDevice::OrbbecInternalDevice()::$_0, ...>::operator()(...)
  std::__1::__function::__func<ob::Context::setDeviceChangedCallback(...)::$_0, ...>::operator()(...)
  libobsensor::DeviceManager::onDeviceChanged(...)
  std::__1::__function::__func<libobsensor::DeviceManager::DeviceManager(bool)::$_1, ...>::operator()(...)
  libobsensor::NetDeviceEnumerator::onPalDeviceChanged(...)
  std::__1::__function::__func<libobsensor::NetDeviceEnumerator::NetDeviceEnumerator(...)::$_0, ...>::operator()(...)
  std::__1::__thread_proxy<..., libobsensor::pal::NetDeviceWatcher::start(...)::$_1>(void*)
  _pthread_start / thread_start  (libsystem_pthread.dylib)
  ```
- Only images on the stack: `liborbbec_scan.3.5.3.dylib`, `libOrbbecSDK.1.7.1.dylib`,
  `libsystem_pthread.dylib`. No `libomp`, no `obscan_context_destroy()` frame.

So: this run reproduces, for a third time (plan8 run, run2, run3), the same
fault — a NULL dereference in `OrbbecDevice::get_active_device()`, reached via
the vendor's internal device-changed → device-connect-callback chain
(`bind_device_connect_callback`), running on the **NetDeviceWatcher thread**,
not the main/registration thread. Consistent with the existing analysis's
`[inferred]` reading: exercising the OQ-3 remove-then-re-add path leaves the
vendor's device bookkeeping in a state where this internal callback's
`get_active_device()` call dereferences NULL, racing against teardown. Timing
here is consistent with that: the raw log's `ObException.cpp:5 Trying to
create a device that you've already created!` line (line 80) and the second
`Pioneer device created!` burst both follow the re-add (check 7, line 77-78);
the crash follows immediately after.

## Relevance to OQ-4

This run is **two data points in one, pointing opposite ways**:

1. **The test's own registered listener** (`OnDeviceMessage`) only ever fired
   on the registration thread in this run — all three observed deliveries,
   across all three phases, same thread hash as registration. That is a third
   consecutive all-registration-thread result for the listener itself, and on
   its own leaves OQ-4's async/watcher-thread half **still open** — no
   observed instance of `OnDeviceMessage` firing anywhere but the
   registration thread.
2. **But** the crash report shows the vendor's *internal* device-connect
   callback plumbing — the mechanism that would, if it got further,
   presumably reach the registered listener — executing on a genuinely
   different thread, the NetDeviceWatcher thread (OS tid `15056758`, never
   `15056739`). It just never got far enough to re-invoke (or fail to
   re-invoke) this test's listener before crashing, so it doesn't settle
   whether `OnDeviceMessage` itself can be called from that thread — only
   that the SDK's internal callback chain genuinely runs there, at least for
   the connect-status bookkeeping step that crashed.

Net: this run does **not** newly resolve OQ-4's async half — no delivery to
the test's own listener was observed off the registration thread, so the
"never exercised" conclusion from the prior run stands for a third run in a
row. It does add a reproduction (third instance) of the known, already
root-caused `get_active_device()` NULL-deref crash on the NetDeviceWatcher
thread, strengthening the case that this specific teardown-adjacent race is
deterministic-ish under the OQ-3 remove/re-add sequence, and that the
internal connect-callback machinery itself is not confined to the
registration thread — only the delivery to *this test's listener*, uniformly,
has been.

## Open questions carried forward

- OQ-4's async/watcher-thread half for `OnDeviceMessage` specifically: still
  unresolved after three runs. A run with a genuinely idle wait window where a
  physical connect/disconnect happens *during* the 2 s sleep (rather than as a
  side effect of the test's own `add`/`remove` calls) might settle it, but
  that would require a manual physical reconnect during the window, which was
  not attempted here (no device action was requested or performed).
- The `get_active_device()` NULL-deref crash (PLAN #11) remains unfixed and is
  not this agent's to fix — reported only.
