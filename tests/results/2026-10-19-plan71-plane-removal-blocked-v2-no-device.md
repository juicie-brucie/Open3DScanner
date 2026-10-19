# PLAN #71 — plane-removal toggle observation, re-run attempt v2

**Date**: 2026-10-19
**Worktree**: `Open3DScanner-wt-hw`, branch `hw/next`.
**Target**: `build/tools/scan_server/scan_server --live`
**Operator**: hw-test subagent.
**Context**: re-run of `2026-10-19-plan71-plane-removal-blocked.md`, requested
because the prior run's baseline and plane-removal clouds were both 0 points
throughout (no subject in the working volume). This re-run was requested with
the user stated to be actively present with a subject placed in the scanner's
view. As a subagent with no interactive channel back to the user mid-run, I
treated that statement in the task itself as the required confirmation (there
was no way to re-query the user once the run started); I did watch the first
~13s of output closely as instructed and stopped relying on it the moment the
log showed a problem, rather than running the full 30s blind.

## Preconditions checked

- `pgrep -x scan_server` / `-x live-worker` / `-x raptor_client` / `-x OpenScanner`:
  all empty before starting — no conflicting SDK user (rule 13: name-anchored).
- `~/Library/Logs/DiagnosticReports/` checked before starting: most recent
  `scan_server-*.ips` timestamped Oct 19 14:29, well before this run (14:59+).

## Binary freshness

`tools/scan_server/main.cpp` mtime (14:59, working-tree) was newer than the
existing `build/tools/scan_server/scan_server` binary (14:25, matching the last
commit `ba33c54` at 14:25:18). Ran `cmake --build build --target scan_server`
before testing (per rule 12/#107) — this time it was **not** a no-op: it
recompiled `main.cpp.o`, relinked `scan_server`, and re-signed/refreshed the
app bundle's copy (`build/tools/macos_app/OpenScanner.app/.../scan_server`).
Both binaries confirmed at mtime 14:59 after the build, before the run below.
`git status --short tools/scan_server/main.cpp` showed no uncommitted diff, so
the mtime gap was from a checkout/worktree-sync operation, not an edit by me.

## Run (baseline, no env vars)

```
./build/tools/scan_server/scan_server --live 8787 "" 30
```
Raw log: `tests/results/2026-10-19-plan71-plane-removal-v2-no-device.log`.

Interposer confirmed loaded (`[vulkan_disable_interpose] loaded`, line 2).

Observed in the first ~8s, well inside the 5-10s early-check window specified
for this re-run:

```
[worker] obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=0
[worker] No device detected — nothing to stream.
[*** LOG ERROR #0001 ***] [2026-10-19 14:59:53] [OrbbecSDK] {mutex lock failed: Invalid argument}
libc++abi: terminating due to uncaught exception of type std::__1::system_error: mutex lock failed: Invalid argument
[launcher] Worker crashed or timed out inside the vendor SDK (child killed by signal Abort trap: 6).
```

This is **not** the previously-seen "tracking SUCCESS but num_points=0" case
from the first attempt — it is a step earlier: the SDK's own device count
came back **0** (`obscan_device_management_get_device_num -> ... count=0`),
i.e. no camera was enumerated as a usable device at all this time, despite
the GVCP discovery log earlier showing the device's identity
(`Creality Raptor Pro`, `CR0886100SN`) being seen on the network. The worker's
own "no device detected" path then hit an uncaught vendor exception
(`mutex lock failed: Invalid argument`) and the process aborted (SIGABRT)
rather than exiting cleanly.

A macOS crash report was generated and has been archived:
`tests/results/2026-10-19-plan71-plane-removal-v2-no-device.crash.ips`
(`scan_server-2026-10-19-145954.ips`, `bug_type: 309`, process `scan_server`).
This is a distinct failure from the known #89/#106 vendor `context_destroy`
hang — that hang is a teardown-stage freeze with no abort; this is an abort
during the "no device" path before any scan ever started.

Per rule 8 (never retry a timeout/hang) and the task's explicit instruction to
stop early rather than run the full duration blind if points/device status
look wrong in the first several seconds, **no second (plane-removal-enabled)
run was attempted.** The device never reached a streaming state in this run,
so there is no baseline cloud to compare against, and the crash itself is a
new observation that needs attention before any further live attempt.

## Checks

| # | Check | Result |
|---|-------|--------|
| 1 | Interposer loaded (banner present) | [PASS] |
| 2 | Binary fresh against source (rebuild fired and matched) | [PASS] |
| 3 | No conflicting SDK process before start | [PASS] |
| 4 | Device detected / enumerated | **[FAIL]** `count=0`, "No device detected" |
| 5 | Baseline per-tick point-count series captured | **[BLOCKED]** — crashed before any tick loop ran |
| 6 | Plane-removal-enabled run attempted | **[not attempted]** — gated on baseline per rule 8 / early-stop instruction |
| 7 | Clean process exit | **[FAIL]** — `Abort trap: 6` on an uncaught `std::system_error` from the vendor SDK |
| 8 | Crash report archived | [PASS] — `tests/results/2026-10-19-plan71-plane-removal-v2-no-device.crash.ips` |

## Verdict

**BLOCKED.** This re-run did not reach the point where #71's owed comparison
(baseline vs. plane-removal point-count series) could even be attempted: the
SDK reported zero devices and then aborted with an uncaught vendor exception
before the stream loop started. This is different from, and arguably more
concerning than, the first attempt's result (which at least showed healthy
device tracking with an empty cloud). Possible causes, not distinguished by
this session: the physical device/USB-network link was not actually connected
or ready at the moment this process started (independent of whether a subject
was placed in front of it — a subject in the working volume does not help if
the camera itself isn't enumerated), or a transient SDK/driver state left over
from the prior session's runs. I did not power-cycle or replug anything (rule
9) and did not retry (rule 8) given the abort.

## Open questions

- Is the Creality Raptor Pro device physically connected and powered on right
  now? The GVCP discovery log did see its identity broadcast on the network
  (`CR0886100SN`) but `obscan_device_management_get_device_num` still returned
  0 — worth checking the physical USB/network link and device power state
  before the next attempt.
- Should the vendor's uncaught-exception-on-no-device path (`mutex lock
  failed: Invalid argument` → `SIGABRT`) be filed as its own plan item? It is
  a distinct crash signature from #89/#106 and is reproducible evidence of a
  vendor SDK defect on the no-device path, independent of #71's own scope.
- #71's owed point-count-drop comparison is still not completed. A third
  attempt should first confirm the device enumerates (`count` > 0) before
  spending the 30s run.

## Board move

#71: stays at `HW` — unchanged from the first attempt's recommendation. The
toggle binding itself (#71's actual scope) was not even exercised this run
since the device never reached a streaming state; this result neither
confirms nor regresses the toggle binding. Recommend checking the physical
device connection before the next attempt, and considering a separate plan
item for the no-device `SIGABRT` crash signature.

## Files

- `tests/results/2026-10-19-plan71-plane-removal-blocked-v2-no-device.md` (this file)
- `tests/results/2026-10-19-plan71-plane-removal-v2-no-device.log` (raw log)
- `tests/results/2026-10-19-plan71-plane-removal-v2-no-device.crash.ips` (archived crash report)
