# PLAN #71 — plane-removal toggle observation (per-tick point-count comparison)

**Date**: 2026-10-19
**Worktree**: `Open3DScanner-wt-hw`, branch `hw/next`, HEAD `009a199`.
**Target**: `build/tools/scan_server/scan_server --live`
**Operator**: hw-test subagent.

## Binary freshness

`build/tools/scan_server/scan_server` mtime Oct 19 14:25; `tools/scan_server/main.cpp`
last changed `a3c4f0d` (2026-10-19 02:23:42 -0400). Ran `cmake --build build --target
scan_server` immediately before testing — every dependency reported `Built target …`,
no rebuild fired, confirming the bundle already matched source (checked per #107).

## Preconditions checked

- `pgrep -x scan_server` / `-x live-worker` / `-x raptor_client` / `-x OpenScanner`:
  all empty before starting — no conflicting SDK user (rule 13: name-anchored).
- Checked `~/Library/Logs/DiagnosticReports/` before starting: most recent
  `scan_server-*.ips` timestamped Oct 19 14:28–14:29, well before this run (14:54+).

## Procedure

Per `docs/recomp/PLAN.md` row #71 and `docs/recon/findings/plane-removal.md` §3: the
three toggles (`obscan_scan_plane_remove`, `obscan_scan_turntable_plane_remove`,
`obscan_scan_config_manaul_remove_plane`) are bound in `tools/scan_server/main.cpp`
behind env vars (`OPENSCANNER_PLANE_REMOVE`, `OPENSCANNER_TURNTABLE_PLANE_REMOVE`,
`OPENSCANNER_MANUAL_PLANE=a,b,c,d`), applied right after `obscan_scan_start`. The
owed observation (per #71's "Owed" note): run with and without the toggles and
compare the per-tick point-count series (`[worker] tick N/M: color_cloud
num_points=...`), printed every 50 ticks.

**Run 1 (baseline, no env vars)**:
```
./build/tools/scan_server/scan_server --live 8787 "" 30
```
Raw log: `tests/results/2026-10-19-plan71-plane-removal-baseline.log`. Exit code 0
(`[launcher] Worker exited normally with code 0.`). Interposer confirmed loaded
(`[vulkan_disable_interpose] loaded`, line 2). `tracking_status samples=1021
histogram: SUCCESS=1021` — the device tracked successfully throughout. Final mesh:
`obscan_scan_get_realtime_model_mesh -> OB_SCAN_SUCCESS (0), num_vertices=0,
num_triangles=0`. This run, unlike the day's other two runs (#105, #66), did **not**
hit the known vendor `context_destroy` hang — `obscan_context_destroy: 1 poll(s), 0
refusal(s), 0.1s, final OB_SCAN_SUCCESS`, a clean teardown.

Per-tick series (every line present, all identical):
```
tick 0/300:   color_cloud num_points=0 (max seen so far: 0)
tick 50/300:  color_cloud num_points=0 (max seen so far: 0)
tick 100/300: color_cloud num_points=0 (max seen so far: 0)
tick 150/300: color_cloud num_points=0 (max seen so far: 0)
tick 200/300: color_cloud num_points=0 (max seen so far: 0)
tick 250/300: color_cloud num_points=0 (max seen so far: 0)
```

**Run 2 (plane removal enabled)**:
```
OPENSCANNER_PLANE_REMOVE=1 OPENSCANNER_TURNTABLE_PLANE_REMOVE=1 \
OPENSCANNER_MANUAL_PLANE="0,0,1,-500" \
./build/tools/scan_server/scan_server --live 8787 "" 30
```
Raw log: `tests/results/2026-10-19-plan71-plane-removal-enabled.log`. Exit code 0.
Interposer confirmed loaded. All three setter calls returned success:
```
obscan_scan_plane_remove(true)                -> OB_SCAN_SUCCESS (0)
obscan_scan_turntable_plane_remove(true)      -> OB_SCAN_SUCCESS (0)
obscan_scan_config_manaul_remove_plane(a,b,c,d) -> OB_SCAN_SUCCESS (0)
```
Per-tick series — identical shape to run 1, all zero:
```
tick 0/300:   color_cloud num_points=0
tick 50/300:  color_cloud num_points=0
tick 100/300: color_cloud num_points=0
tick 150/300: color_cloud num_points=0
tick 200/300: color_cloud num_points=0
tick 250/300: color_cloud num_points=0
```
Clean teardown again, no vendor hang this run.

## Checks

| # | Check | Result |
|---|-------|--------|
| 1 | Interposer loaded (banner present) | [PASS] both runs |
| 2 | Binary fresh against source | [PASS] confirmed via rebuild no-op |
| 3 | Toggles callable live, all three return `OB_SCAN_SUCCESS` | [PASS] |
| 4 | Baseline (toggles off) per-tick point-count series captured | [PASS] — captured, but uniformly 0 |
| 5 | Plane-removal-on per-tick point-count series captured | [PASS] — captured, but uniformly 0 |
| 6 | Point counts visibly drop with plane removal on vs. baseline | **[BLOCKED]** — both series are 0 throughout; no non-zero baseline cloud exists to show a drop from |
| 7 | No crash report generated | [PASS] (none in `~/Library/Logs/DiagnosticReports/` for either run's window) |

## Verdict

**BLOCKED** — not PASS, not FAIL. The toggle *binding* itself works exactly as
documented: all three setter calls accept live and return `OB_SCAN_SUCCESS` in both
the step-free gate check and in practice, matching `plane-removal.md` §1/§3. But the
actual observation #71 asks for — **a visible point-count drop when plane removal is
enabled** — is not reachable from this session's data: the live model cloud
(`obscan_scan_get_model_point_color_clouds`) reported exactly 0 points on every
sampled tick in *both* runs, and the final mesh was empty (`num_vertices=0,
num_triangles=0`) in run 1. With a baseline of 0, there is nothing for plane removal
to visibly remove, so the comparison the row calls for cannot be demonstrated either
way — this is a "not observed" result, not a "no effect" result.

This is a genuine behavioral difference from two prior runs today's referenced
precedent (`2026-10-18-scan-server-scale-teardown-acceptance.md`, which saw
`num_points` climb from 0 to 1.4M+ over a similar session), so it is flagged as
**unexpected** rather than pushed through. It is not the known #89/#106
`context_destroy` hang (both runs here tore down cleanly, exit 0) — it is a distinct
observation: the device tracked successfully (`tracking_status` histogram all
`SUCCESS`) but accumulated no model geometry. The most likely explanation is a
physical-setup difference (nothing stable in the scanner's working range, or the
device/turntable not positioned the way prior successful scans were) rather than an
SDK or toggle defect, but that is a hypothesis, not a confirmed cause — no camera/
visual check was possible from this session.

## Open questions

- Was there an object positioned in the scanner's working volume for this run? Prior
  runs with non-zero point accumulation (e.g. plan32/scale-teardown) imply a subject
  was present and/or the turntable was active. This run's physical setup was not
  verified by a human before starting.
- Should #71's observation be re-attempted with a confirmed subject in frame before
  the next session, so a genuine baseline-vs-plane-removal point-count comparison can
  be captured?

## Board move

#71: stays at `HW` — the toggle binding is reconfirmed working (all three calls
succeed live), but the owed point-count-drop *observation* itself could not be
completed this run because the baseline cloud was empty. Not moved to `DONE`; not
regressed either since nothing failed. Recommend re-running once a subject's
presence in the scanner's frame is confirmed.

## Files

- `tests/results/2026-10-19-plan71-plane-removal-blocked.md` (this file)
- `tests/results/2026-10-19-plan71-plane-removal-baseline.log` (raw log, run 1)
- `tests/results/2026-10-19-plan71-plane-removal-enabled.log` (raw log, run 2)
