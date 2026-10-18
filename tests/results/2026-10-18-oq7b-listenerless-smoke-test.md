# PLAN #62 (OQ-7b) — the listener-less with-device run

**Date:** 2026-10-18
**Worktree:** `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw` (confirmed via `pwd`)
**Branch:** `hw/next` (confirmed via `git branch --show-current`)
**Binary:** `build/tools/smoke_test/smoke_test`
**Device:** Creality Raptor Pro, SN `CR0886100SN`, USB3.0, physically connected
**Raw log:** `tests/results/2026-10-18-oq7b-listenerless-smoke-test.log`
**Crash report:** `tests/results/2026-10-18-oq7b-listenerless-smoke-test.crash.ips`
(copied verbatim from `~/Library/Logs/DiagnosticReports/smoke_test-2026-10-18-191029.ips`,
procLaunch 19:10:21.15 / captureTime 19:10:27.72, matching this run's PID 15439 and
wall-clock window)

## Pre-run checks

- `pwd` → `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`; branch `hw/next`.
  Per CLAUDE.md this is the correct worktree (interposer build + 180 s timeout
  live here, not in the main checkout).
- `ps aux | grep -iE "smoke_test|live_capture_test|scan_server|device_message_listener_test"`
  → no matching processes. No other SDK-using process attached.
- Binary mtime (`Oct 18 19:05`) is newer than `tools/smoke_test/main.cpp` mtime
  (`Oct 17 23:32`) — built from current source, no rebuild performed by me.

## Source read before running (per the task's instruction not to guess)

Read `tools/smoke_test/main.cpp` in full. Findings relevant to this run:

- Usage is `smoke_test [path-to-lib]`; with no arg it calls
  `ObscanLibrary::FindDefaultLibraryPath()`. Ran with no arg.
- `grep -rln "setenv" engine/ tools/` → only `engine/src/worker_process.cpp`,
  `tools/scan_server/main.cpp`, `tools/live_capture_test/main.cpp`.
  **`tools/smoke_test/main.cpp` never calls `setenv`** and never sets
  `DYLD_INSERT_LIBRARIES`. `engine/src/obscan_loader.cpp` likewise has no
  reference to `setenv`/`DYLD`/`interpose`/`MoltenVK`. So — unlike
  `scan_server --live`, which is a different tool that does set it internally —
  **`smoke_test` does not load the Vulkan-disabling interposer at all.** This
  is the tool's own documented worker-isolation design (see its header comment
  and `worker_process.h`): it isolates the *risky call itself*
  (`obscan_context_destroy`) in a disposable forked/re-exec'd child so a crash
  there doesn't take down the launcher, rather than trying to prevent the
  underlying vendor bug the way `live_capture_test`/`scan_server` do. There is
  no interposer banner to check for this binary — confirmed by source reading,
  not assumed. (A cross-check of `tests/results/2026-10-17-live-scan-manual-test.md`
  shows the interposer banner does appear for `scan_server --live`, confirming
  the convention really is per-tool, not universal.)
- Confirmed by source reading that `tools/smoke_test/main.cpp` has **no call** to
  `obscan_device_management_add_device_msg_listener` anywhere in the file (own
  grep of the file, matching the task's own prior grep claim).

## Command and exit code

```
./build/tools/smoke_test/smoke_test
```

Output (full, unredacted) is in the raw log. Final two launcher lines:

```
[10/18 19:10:25.918527][info][16168167][Context.cpp:89] Context destroyed
...
[launcher] Worker crashed inside the vendor SDK (child killed by signal Segmentation fault: 11). This is a known upstream issue (see README.md "Known issues") — the launcher process is unaffected and is exiting cleanly instead of propagating the crash.
```

**Exit code: 2** (not captured live via `$?`/`PIPESTATUS` due to a shell/tee
interaction, but certain from source: `main.cpp`'s crash-recovery branch,
reached exactly when `RunSelfAsWorker` reports `worker_ok == false`, is
`return 2;` — and the printed "Worker crashed inside the vendor SDK ... exiting
cleanly" line only comes from that branch). Per CLAUDE.md hard rule 10, exit 2
is documented elsewhere as "skipped (no SDK dylib)" — **that is a different
tool's convention.** In `smoke_test`'s own code, exit 2 is deliberately
overloaded to mean "recovered from a known vendor crash in the worker," per
its own comment (`main.cpp` line ~162-164). This run is **not** a skip: the
dylib loaded, the version call succeeded, the context was created, and the
crash happened well into a real SDK session. Flagging this because rule 10's
wording, read carelessly, could cause a future run to be mis-filed as "skipped."

This is a **crash**, not a hang — no 180 s timeout was hit, no retry was
needed, and rule 8 ("never retry a timeout") does not apply here. The crash
report at `~/Library/Logs/DiagnosticReports/smoke_test-2026-10-18-191029.ips`
was located and archived into `tests/results/` per rule on crash reports.

## The `get_device_num()` line (verbatim)

```
[worker] obscan_device_management_get_device_num() -> OB_SCAN_SUCCESS, count=0
```

Status: `OB_SCAN_SUCCESS`. Count: **0**.

## Listener-absence confirmed from the run's own call sequence

Grepping the raw log for listener/context/device-watcher lines:

```
obscan_context_create
[worker] obscan_context_create() -> OB_SCAN_SUCCESS
[worker] obscan_device_management_get_device_num() -> OB_SCAN_SUCCESS, count=0
[10/18 19:10:24.908206][info][...][Ethernet.cpp:69] NetDeviceWatcher detected new device, uid:30:30:34:30:35:30
[10/18 19:10:25.917435][info][...][DeviceManager.cpp:188] Device changed! removed: 0, added: 1
```

No `add_device_msg_listener` call (nor any other `*listener*` string) appears
anywhere in the printed call sequence or the vendor SDK's own log lines. This
matches the prior grep of `main.cpp`: this tool never registers a
device-message listener. Confirmed absent, not just assumed.

## Reading the timestamps — why this does **not** cleanly answer OQ-7b

This is the important nuance, and it is not the simple "stayed 0" vs.
"went nonzero" binary the task framed. Lining up timestamps:

| Time | Event |
|---|---|
| 19:10:23.879444 | `Context created` (vendor log) |
| 19:10:23.879477 | `Current found device(s): (0)` — vendor's own internal count at context-create time |
| ~19:10:23.880 | `[worker] obscan_device_management_get_device_num() -> OB_SCAN_SUCCESS, count=0` — **smoke_test's one and only check** |
| 19:10:24.908206 | `NetDeviceWatcher detected new device` |
| 19:10:25.917435 | `Device changed! removed: 0, added: 1` — vendor's internal device list flips to 1 |
| 19:10:25.917801 | vendor log lists the device by name/SN (`CR-Scan Raptor Pro`, `CR0886100SN`) |
| 19:10:25.918527 | `Context destroyed` (smoke_test proceeds immediately to teardown) |
| ~19:10:27 (during destroy) | MoltenVK/Vulkan device creation for the SuperPoint network, then SIGSEGV |

`smoke_test` calls `get_device_num()` exactly once, immediately after
`obscan_context_create()` returns — roughly 1 second before the GVCP/network
watcher's own asynchronous detection completes (the watcher logs the device at
+1.03 s, and the vendor's internal device list only flips to count 1 at
+2.04 s). By the time `smoke_test` reached `obscan_context_destroy()`, the
device **had** been detected internally by the vendor SDK (its own log shows
count 1, device named and enumerated) — but `smoke_test` never called
`get_device_num()` again to observe that, so this run never captured a
nonzero return from that specific API with no listener registered.

So:
- This run **does corroborate** the PLAN #7 model that the watchers
  (USB hotplug + GVCP) run and succeed in detecting the physical device
  independently of any listener registration — the vendor's own internal
  `DeviceManager.cpp:188 Device changed! added: 1` and the named-device log
  line prove detection happened, with zero listener calls anywhere in this
  run's call sequence.
- This run does **not** demonstrate that `obscan_device_management_get_device_num()`
  itself returns nonzero without a listener, because `smoke_test` only polled
  it once, before detection had finished. The `count=0` result here is fully
  explained by a race against the watcher's ~1-2 s async detection latency —
  not by listener absence.

**Verdict on OQ-7b: reopened/not retired, but not as "contradicts the PLAN #7
model."** The honest state is: OQ-7b's narrow factual question — "does
`get_device_num()` become nonzero with no listener ever registered" — is
**still not directly observed**, because this tool's single pre-detection
poll cannot distinguish "gated by listener absence" from "polled too early."
A definitive answer needs a listener-less run that calls `get_device_num()`
again after a short delay (or in a loop) following `context_create()`, still
never registering a listener, and observes whether that second/later call
returns nonzero. `smoke_test` as written cannot do this without modification,
and modifying the test is out of scope for this agent.

## Crash note (separate from the OQ-7b question, but part of the record)

`obscan_context_destroy()` crashed with SIGSEGV inside the worker during
MoltenVK/Vulkan device creation for the vendor's SuperPoint network — this is
the same documented upstream defect in `README.md` ("Known issues": vendor's
ncnn+OpenMP/Vulkan teardown path can crash or hang). `smoke_test` does **not**
load `engine/src/vulkan_disable_interpose.c` (confirmed above — no `setenv`
anywhere in its path), so it has no protection against this defect the way
`live_capture_test`/`scan_server --live` do. The worker-isolation design
(fork + re-exec) did its job: the launcher process was unaffected, reported
the crash accurately, and exited cleanly (code 2) rather than propagating a
signal or hanging. No power cycle was needed — the device was left in a
state where nothing further was asked of it after the crash. The crash report
was located in `~/Library/Logs/DiagnosticReports/` by timestamp/PID match and
archived alongside this record.

## Checks table

| Check | Result |
|---|---|
| Correct worktree/branch confirmed before first command | [PASS] |
| No other SDK-using process attached (checked by process name, not substring) | [PASS] |
| Binary not stale relative to source | [PASS] |
| Interposer/launcher banner convention checked by reading source, not assumed | [PASS] — this tool has none; confirmed absent from its call chain |
| Listener never registered, confirmed from printed call sequence | [PASS] — no `*listener*` string anywhere in the log |
| `get_device_num()` exact line captured | [PASS] — `OB_SCAN_SUCCESS, count=0` |
| Run exited non-cleanly (SIGSEGV in worker) | [FAIL] (of the underlying vendor SDK; not a fault of this test or this agent) |
| Crash report located and archived | [PASS] — `tests/results/2026-10-18-oq7b-listenerless-smoke-test.crash.ips` |
| OQ-7b definitively answered either direction | **Not answered** — see "Reading the timestamps" above; mislabel risk flagged |

## Open questions for the planner

1. Does OQ-7b's board entry need rewording to ask specifically about a
   *delayed or repeated* `get_device_num()` call with no listener, since a
   single immediate call (as `smoke_test` makes) is provably confounded by
   the watcher's own ~1-2 s async detection latency? The vendor log itself
   proves detection succeeds with no listener in well under 2.1 s; the open
   part is only whether the *public getter* reflects that without a listener,
   which this run could not isolate.
2. `smoke_test`'s crash-recovery exit code (2) collides in meaning with the
   "skipped, no SDK dylib" exit code 2 convention cited in CLAUDE.md hard rule
   10. Worth flagging so a future run isn't mis-filed as a skip when it's
   actually a recovered crash with a real session underneath it.
3. Should any future listener-less probe tool budget for the SIGSEGV this run
   hit in `obscan_context_destroy()` (e.g. wrap its own run in the same
   worker-isolation `smoke_test` already uses)? This run's crash is additional
   confirmation of the documented defect, now also observed with a
   listener-less call sequence (previously only confirmed with a listener
   registered, e.g. in `scan_server`/`live_capture_test` sessions that use the
   interposer specifically to avoid it).
