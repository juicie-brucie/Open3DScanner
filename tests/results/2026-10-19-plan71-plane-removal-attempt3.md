# PLAN #71 — plane-removal toggle observation, re-run attempt 3

**Date**: 2026-10-19
**Worktree**: `Open3DScanner-wt-hw`, branch `hw/next`.
**Target**: `build/tools/scan_server/scan_server --live`
**Operator**: hw-test subagent.
**Context**: third attempt, run after the user checked the Raptor Pro's
physical USB/network connection and power following attempt 2's SIGABRT
(device count=0 despite GVCP seeing the device on the network).

## Preconditions checked

- `pgrep -x scan_server` / `-x raptor_client` / `-x OpenScanner`: all empty
  before starting. One stale PID turned up from a broad `pgrep -af` substring
  grep used only for investigation (never for the gating decision, per rule
  13) — `ps -p` on it returned nothing, confirming it was already gone, not a
  live conflicting process.
- `~/Library/Logs/DiagnosticReports/`: most recent `scan_server-*.ips` before
  this run was `scan_server-2026-10-19-145954.ips` (14:59, attempt 2's crash).
  No new crash report appeared during or after any of this run's steps.

## Mid-task correction addressed

The coordinator asked me to verify whether a separate "OpenScanner app"
window/process was running, distinguishing "app never launched" from "app
launched but device didn't enumerate." Checked: `pgrep -x OpenScanner` found
nothing, and `ps -p` on a stale PID returned by a substring `pgrep -af
"OpenScanner.app"` confirmed it was not live either. **No OpenScanner.app GUI
process was running at any point in this session, by design** — #71's target,
consistent with attempts 1 and 2 and the task instructions, is the
`scan_server --live` CLI tool invoked directly (`./build/tools/scan_server/
scan_server --live 8787 "" <seconds>`), not the macOS app bundle. The CLI tool
owns its own SDK context/session directly in-process; there is no separate
app window to launch for this test target. This is not an app-launch failure
mode — it's a different binary than the GUI app.

## Binary freshness

`tools/scan_server/main.cpp` and both `build/tools/scan_server/scan_server`
and the app bundle's copy were all at mtime Oct 19 14:59 (last refreshed
during attempt 2, per that record). `git status --short tools/scan_server/
main.cpp` showed no uncommitted diff. Ran `cmake --build build --target
scan_server` before this attempt: build was a no-op (`Built target
scan_server`, no recompile step fired) — confirms the already-fresh 14:59
binary is still current, checked against source per rule 12.

## Step 1: lingering process / port check

No `scan_server`, `raptor_client`, or `OpenScanner` processes found running
(name-anchored `pgrep -x`, rule 13). Nothing killed.

## Step 2: quick device-count check

First attempt at 5s (`scan_server --live 8787 "" 5`) exited cleanly (no crash,
no watchdog-kill message) but the process ended before the worker reached the
`obscan_device_management_get_device_num` call — GVCP discovery was still in
progress when the 5s window closed. This was **not a timeout/hang** (clean
exit, no abort), so per rule 8 it was not a retry of a timeout — it was an
under-provisioned check duration. Re-ran with 10s:

```
./build/tools/scan_server/scan_server --live 8787 "" 10
```

Result: GVCP discovery completed (`Creality Raptor Pro`, `CR0886100SN` seen
and connected), and:

```
[worker] obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1
[worker] obscan_scan_start                             -> OB_SCAN_SUCCESS (0)
[worker] tick 0/100: color_cloud num_points=0 (max seen so far: 0), clients=0, paused=0
```

**Device enumerated (count=1)** — a change from attempt 2's `count=0`. No
crash. Proceeded to the full procedure.

## Step 3: baseline run (env vars unset)

```
./build/tools/scan_server/scan_server --live 8787 "" 30
```
Raw log: `tests/results/2026-10-19-plan71-plane-removal-attempt3-baseline.log`

Interposer confirmed loaded (`[vulkan_disable_interpose] loaded`, line 2).
Device enumerated (`count=1`). Per-tick point counts grew over the run
(real points, not an empty scene):

| tick | num_points |
|---|---|
| 0/300 | 0 |
| 50/300 | 309 |
| 100/300 | 420 |
| 150/300 | 495 |
| 200/300 | 529 |
| 250/300 | 7376 |

Exit: `[launcher] Worker exited normally with code 0.` `obscan_context_destroy
-> OB_SCAN_SUCCESS`. `our_plane_removal fits=0 kept=0 cut=0` (plane-removal
env vars unset, as expected for baseline). No crash report generated.

## Step 4: plane-removal-enabled run

```
OPENSCANNER_PLANE_REMOVE=1 OPENSCANNER_TURNTABLE_PLANE_REMOVE=1 \
OPENSCANNER_MANUAL_PLANE="0,0,1,-500" \
./build/tools/scan_server/scan_server --live 8787 "" 30
```
Raw log: `tests/results/2026-10-19-plan71-plane-removal-attempt3-enabled.log`

Confirmed toggle calls fired:
```
[worker] obscan_scan_plane_remove(true)                -> OB_SCAN_SUCCESS (0)
[worker] obscan_scan_turntable_plane_remove(true)      -> OB_SCAN_SUCCESS (0)
```

Per-tick point counts:

| tick | num_points |
|---|---|
| 0/300 | 0 |
| 50/300 | 3805 |
| 100/300 | 90484 |
| 150/300 | 161294 |
| 200/300 | 206038 |
| 250/300 | 284851 |

Exit: `[launcher] Worker exited normally with code 0.` `obscan_context_destroy
-> OB_SCAN_SUCCESS`. `our_plane_removal fits=0 kept=0 cut=0` — **identical to
baseline**: the manual-plane cut logic itself never fit/kept/cut any points
in either run. No crash report generated.

## Comparison

The toggle **binding** (`obscan_scan_plane_remove`/
`obscan_scan_turntable_plane_remove` calls) fires successfully and only in
the enabled run, consistent with attempt 1. However this is **not a
controlled A/B of the same scene**: the enabled run's point counts are an
order of magnitude *larger* than baseline's at matching ticks (e.g. tick
200/300: 529 vs 206038), which reflects two independent live scans of
whatever was in front of the camera at that moment, not a plane-removal
effect — the counts are both accumulating model-cloud totals, not a frame
delta, and nothing about the run that controls scene content was held
constant between the two sequential invocations. The owed comparison (does
enabling plane removal reduce the published point count against an otherwise
identical scene) is not resolved by this data: `our_plane_removal fits=0
kept=0 cut=0` in both runs shows the manual-plane geometric cut itself never
activated in either run, so no cutting behavior was exercised to compare.

## Checks

| # | Check | Result |
|---|-------|--------|
| 1 | Interposer loaded (banner present), both runs | [PASS] |
| 2 | Binary fresh against source (rebuild no-op, already current) | [PASS] |
| 3 | No conflicting SDK process before start | [PASS] |
| 4 | Device enumerated (`count` > 0) | [PASS] `count=1` (was `count=0` in attempt 2) |
| 5 | Baseline per-tick point-count series captured, real points | [PASS] |
| 6 | Plane-removal-enabled run completed | [PASS] |
| 7 | Toggle binding calls fire correctly (enabled only when set) | [PASS] |
| 8 | Clean process exit, both runs | [PASS] exit code 0 |
| 9 | No crash in either run | [PASS] — no new `.ips` file appeared |
| 10 | Manual-plane cut logic exercised (`fits`/`kept`/`cut` > 0) | **[FAIL]** `fits=0 kept=0 cut=0` in both runs — not exercised |
| 11 | Point-count-drop comparison (enabled vs baseline, same scene) | **[not resolved]** — runs were not a controlled A/B of identical scene content |

## Verdict

**PASS (partial)** for the device-enumeration and toggle-binding scope: the
device enumerated and streamed cleanly this time (attempt 2's blocker is
resolved — the physical connection fix worked), both runs completed without
crashing, and the `obscan_scan_plane_remove`/`obscan_scan_turntable_plane_remove`
binding fires correctly only when the env vars are set. **BLOCKED** on #71's
actual owed comparison: the manual-plane cut logic (`our_plane_removal`)
never fit/kept/cut any points in either run, so no plane-removal effect on
point counts was observed to compare — either `OPENSCANNER_MANUAL_PLANE="0,0,1,-500"`
does not match this scene's actual plane geometry, or the cut path needs a
condition not met by this scene/pose. This is a different open question than
attempts 1 and 2 raised.

## Open questions

- Why does `our_plane_removal fits=0 kept=0 cut=0` in every attempt so far
  (1, 2 didn't reach it; this one reached it and still got zero)? Is the
  manual plane equation value wrong for this camera's coordinate frame /
  this scene's actual table height, or is there another precondition for the
  cut path to engage?
- A genuine baseline-vs-enabled comparison needs the *same* scene/pose held
  across both runs (e.g. a static object, scanner not touched between runs)
  to isolate the plane-removal effect from scene-to-scene variation — this
  attempt's two runs saw very different point-count growth curves, likely
  because the physical scene changed between them.

## Board move

#71: recommend moving from `HW` to `HW` (unchanged) but with a narrowed
scope note: the toggle-binding mechanics are now confirmed working on real
hardware ([hw-confirmed: this run]), but the actual point-removal effect is
still unconfirmed because the manual-plane cut never activates. Suggest the
next attempt either picks a `OPENSCANNER_MANUAL_PLANE` value verified against
this camera's real depth units/axis convention, or adds a log line exposing
why `fits` stayed 0, before spending another live run on this.

## Files

- `tests/results/2026-10-19-plan71-plane-removal-attempt3.md` (this file)
- `tests/results/2026-10-19-plan71-plane-removal-attempt3-baseline.log` (raw log, baseline)
- `tests/results/2026-10-19-plan71-plane-removal-attempt3-enabled.log` (raw log, plane-removal enabled)
- Prior records untouched: `tests/results/2026-10-19-plan71-plane-removal-blocked.md`,
  `tests/results/2026-10-19-plan71-plane-removal-blocked-v2-no-device.md` (+ its `.crash.ips`/`.log`)
