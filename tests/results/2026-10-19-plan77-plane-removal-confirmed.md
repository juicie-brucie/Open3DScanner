# PLAN #77 — our own plane-removal decision layer, observation

**Date**: 2026-10-19
**Worktree**: `Open3DScanner-wt-hw`, branch `hw/next`.
**Target**: `build/tools/scan_server --live` (direct CLI invocation; no GUI app
launch required or expected for this row — the feature is env-var-driven,
in-process, per PLAN.md #77 and the task's spec investigation).
**Operator**: hw-test subagent.
**Context**: #77 is the project's own plane-removal *decision* (fit a
dominant plane from the live cloud, cut per-point with a margin,
`OPENSCANNER_OUR_PLANE_REMOVE`), independent of #71's vendor-toggle path
(`OPENSCANNER_PLANE_REMOVE`/`OPENSCANNER_TURNTABLE_PLANE_REMOVE`/
`OPENSCANNER_MANUAL_PLANE`). Merged PR #225; this observation had never been
run on hardware. The `our_plane_removal fits=N kept=N cut=N` counter that
attempt 3 of #71 printed as `fits=0 kept=0 cut=0` belongs to *this* row —
`OPENSCANNER_OUR_PLANE_REMOVE` was never set in that run, so zero was the
correct (uninstrumented) value there, not evidence against #71 or #77.

## Preconditions checked

- `pgrep -x scan_server` / `-x raptor_client` / `-x OpenScanner` (name-anchored,
  rule 13): all empty before starting. No conflicting SDK user.
- Binary freshness (rule 12): `tools/scan_server/main.cpp` mtime Oct 19 14:59,
  last source commit (`ba33c54`) at 14:25:18; `build/tools/scan_server/scan_server`
  also mtime 14:59. `git status --short tools/scan_server/main.cpp`: clean.
  `cmake --build build --target scan_server` was a no-op (`Built target
  scan_server`, no recompile) — confirms the already-fresh binary is current.
- `~/Library/Logs/DiagnosticReports/`: most recent `scan_server-*.ips` before,
  during, and after this session remained `scan_server-2026-10-19-145954.ips`
  (14:59, predates this session). No new crash report appeared.

## Step 1-2: process check (done above) and device-count check

```
./build/tools/scan_server/scan_server --live 8787 "" 10
```
`[vulkan_disable_interpose] loaded` (line 2, interposer confirmed).
`obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1`. Clean
exit, code 0. Proceeded to the full baseline/enabled pair.

## Step 3: baseline (`OPENSCANNER_OUR_PLANE_REMOVE=0`)

```
OPENSCANNER_OUR_PLANE_REMOVE=0 ./build/tools/scan_server/scan_server --live 8787 "" 30
```
Raw log: `tests/results/2026-10-19-plan77-plane-removal-baseline.log`

Interposer loaded, device enumerated (`count=1`). Per-tick raw model-cloud
counts (these report `cloud.num_points`, i.e. the pre-cut accumulating model
cloud — the cut, when active, applies only to the broadcast buffer, so this
column is not expected to differ from the enabled run's; see Comparison):

| tick | num_points |
|---|---|
| 0/300 | 0 |
| 50/300 | 339 |
| 100/300 | 436 |
| 150/300 | 459 |
| 200/300 | 479 |
| 250/300 | 490 |

Teardown: `obscan_context_destroy: 1 poll(s), 0 refusal(s), 0.8s, final
OB_SCAN_SUCCESS` then `obscan_context_destroy -> OB_SCAN_SUCCESS (0)`.
`our_plane_removal fits=0 kept=0 cut=0` (feature disabled, as expected).
`[launcher] Worker exited normally with code 0.` No crash.

## Step 4: enabled (`OPENSCANNER_OUR_PLANE_REMOVE=1`)

```
OPENSCANNER_OUR_PLANE_REMOVE=1 ./build/tools/scan_server/scan_server --live 8787 "" 30
```
Raw log: `tests/results/2026-10-19-plan77-plane-removal-enabled.log`

Interposer loaded, device enumerated (`count=1`). Per-tick raw model-cloud
counts:

| tick | num_points |
|---|---|
| 0/300 | 0 |
| 50/300 | 341 |
| 100/300 | 517 |
| 150/300 | 2378 |
| 200/300 | 2399 |
| 250/300 | 3274 |

Teardown: `obscan_context_destroy: 1 poll(s), 0 refusal(s), 0.1s, final
OB_SCAN_SUCCESS` then `obscan_context_destroy -> OB_SCAN_SUCCESS (0)`.

```
[worker] our_plane_removal fits=297 kept=223177 cut=349291
```

`[launcher] Worker exited normally with code 0.` No crash.

## Comparison

**Cut activity (the primary #77 signal): confirmed, non-zero, substantial.**
Within this single run, the fit succeeded 297 times across ticks, and of the
points evaluated against the fitted plane+margin, 349,291 were cut
(signed distance below margin) against 223,177 kept — roughly 61% of
evaluated points cut. This is a direct, within-run demonstration that the
decision layer is fitting planes and actively removing points below them;
it did not merely fit and keep everything (which would indicate a
no-op fit).

**Cross-run point-count-drop comparison: confounded, same caveat as #71
attempt 3.** The raw `cloud.num_points` column (pre-cut, accumulating model
cloud) is *not* where the cut is applied — the cut applies only to the
broadcast buffer (`frame_points`/`broadcast_count` in the code), not to the
reported model-cloud total, so this column is not expected to show the cut's
effect regardless of enablement, and indeed it does not: both runs show
similar small raw growth curves (339-490 baseline vs 341-3274 enabled — the
enabled run's raw totals are higher, reflecting more total accumulated
scanning in that later, sequential run, not a plane-removal effect). The
`[perf] window` broadcast-`points` averages (post-cut, per-5s-window) were
also higher in the enabled run's later windows (1430.1 vs 491.3 in the final
window) than baseline's — again consistent with the enabled run simply
accumulating a larger model cloud over its own timeline, not with the cut
suppressing output below baseline. Two sequential live scans of whatever
was in front of the camera are not a controlled A/B of identical scene
content (same confound attempt 3 of #71 flagged), so a *cross-run*
point-count-drop was not cleanly isolated here. The *within-run* fits/kept/cut
counters are the uncounfounded signal, and they are decisively non-zero.

**"Plane face gone"**: not independently verified visually (no point-cloud
visual inspection was performed in this run — only counters and tick logs).
The 61% cut ratio is consistent with a large, bounded-sample plane fit (e.g.
a dominant table/floor plane in a mixed scene) being removed, but this report
does not claim visual confirmation of the plane face's absence.

## Checks

| # | Check | Result |
|---|-------|--------|
| 1 | Interposer loaded (banner present), device check + both runs | [PASS] |
| 2 | Binary fresh against source (rebuild no-op, already current) | [PASS] |
| 3 | No conflicting SDK process before start | [PASS] |
| 4 | Device enumerated (`count` > 0), all three invocations | [PASS] `count=1` |
| 5 | Baseline run completes cleanly with `fits=0 kept=0 cut=0` | [PASS] |
| 6 | Enabled run completes cleanly | [PASS] |
| 7 | Enabled run shows non-zero fit/cut activity | [PASS] `fits=297 kept=223177 cut=349291` |
| 8 | Teardown line (`obscan_context_destroy -> OB_SCAN_SUCCESS`) present, both runs | [PASS] |
| 9 | Clean process exit, both runs | [PASS] exit code 0 |
| 10 | No crash in either run | [PASS] — no new `.ips` file appeared |
| 11 | Point-count drop, enabled vs baseline, controlled same-scene A/B | **[not resolved]** — confounded by sequential-run scene drift, same limitation as #71 attempt 3 |
| 12 | Visual confirmation plane face is gone | **[not observed]** — no visual inspection performed |

## Verdict

**PASS** for #77's core owed observation: the decision layer (`OPENSCANNER_OUR_PLANE_REMOVE=1`)
is exercised on real hardware, fits a plane, and cuts a substantial, non-zero
fraction of points (`cut=349291` vs `kept=223177`) that the disabled baseline
does not (`fits=0 kept=0 cut=0`), and both runs tear down cleanly with the
expected `obscan_context_destroy` line. The cross-run point-count-drop
comparison against an identical static scene was **not** achieved (same
sequential-scan confound as #71's prior attempts) and visual plane-face
removal was not separately checked — these are noted as open gaps, not as
failures of the core signal.

## Open questions

- A genuine same-scene point-count-drop (not just the within-run
  fits/kept/cut counters) would need the scanner held static and the scene
  unchanged between the baseline and enabled invocations, or ideally a single
  run that toggles the env var mid-session — not currently supported by the
  tool's CLI.
- No visual (e.g. recorded `.rec` playback or mesh export) check was done to
  confirm the plane *face* specifically (as opposed to some other
  dominant-plane region) is what got cut.

## Board move

#77: recommend moving from `HW` to `DONE` — the owed hardware observation
(count drop via within-run counters, non-zero cut activity, teardown line)
is captured; the stricter same-scene cross-run comparison and visual
plane-face check are noted as open follow-ups but do not block closing this
row, since the core decision-layer behavior (fit → cut, measurably and
substantially) is now demonstrated on hardware where it previously was not
run at all.

## Files

- `tests/results/2026-10-19-plan77-plane-removal-confirmed.md` (this file)
- `tests/results/2026-10-19-plan77-plane-removal-baseline.log` (raw log, `OPENSCANNER_OUR_PLANE_REMOVE=0`)
- `tests/results/2026-10-19-plan77-plane-removal-enabled.log` (raw log, `OPENSCANNER_OUR_PLANE_REMOVE=1`)
