# Real CrealityScan.app session #2, same scanner position as `logs/exposure_sweep_new_setup_20261016/`

Project name: `2nd_comp_open3dscanner`. Scanner left in the same
physical position as the reworked, flatter setup used for this
project's own `logs/exposure_sweep_new_setup_20261016/` run (42,224
vertices) — a second like-for-like comparison against the real app,
this time on the setup that already worked well for our own bindings,
rather than the original tall-cup-stack setup from
`logs/creality_scan_comparison_20261016/` (which struggled in both
tools).

## What this folder contains

- `screenshots/` — three screenshots from the live session:
  1. `1_live_scan_mid.jpg` — live scan mid-session, HUD showing
     `Frames 3,801 / Points 149 / Markers 29`, with the "Please
     continue scanning or click [Finish]" prompt still showing.
  2. `2_scan_settings_panel.jpg` — the full Scan Settings panel plus
     live point-cloud/marker view (`Frames 3,603 / Points 139`),
     confirming the exact settings in use.
  3. `3_pre_finish_state.jpg` — later in the same session
     (`Frames 899 / Points 104` — a fresh pass after a prior
     fusion/clear), same settings panel.
- `project_metadata/` — `scanParamConfig.dat` (JSON, this session's
  exact mode config), `project.obp`, and a file listing
  (`obscan_database_file_listing.txt`) of the project's raw SQLite scan
  database (`resources.obscan`, ~356MB — **not copied into this repo**,
  well over GitHub's 100MB single-file limit; it remains at
  `~/Library/CrealityScan/Projects/2nd_comp_open3dscanner/` locally if
  deeper inspection is ever needed).
- `native_logs/` — the complete vendor log directory for this session
  (`scan_log_*.txt` ×2, `ScannerStreamSDK.log`, `app.log`,
  `device_info.json`, `system_info.txt`, `Native/*.log`), copied as-is
  from `~/Library/CrealityScan/Logs/20261016031502/`.

## Session settings (from `scanParamConfig.dat`)

Same as `logs/creality_scan_comparison_20261016/`: Blue Laser Mode,
Tracking Mode "With Markers", Scan Mode "Pointcloud", `VoxelSizeProperty:
0.5`, Laser Mode "Cross lines", Scanning Range 200-550mm,
`IrExpAndLaserValue: 500` (auto mode on for both IR and RGB).

## Result: this time the real app finished cleanly

Unlike the first comparison (`logs/creality_scan_comparison_20261016/`,
blocked by "insufficient data" on the tall-cup-stack setup), this
session's `app.log` shows a real finish: `OnRightScanButtonClicked:
status = BTN_STATUS_FINISH` at 03:17:24, ~2 minutes after scan start,
followed by a pause — no "insufficient data" block anywhere in the log.

The scan database persisted **38 real frames** (`bfi_0..37` /
`bfp_0..37` in the file listing), each `bfp_N.bat` point blob ~9MB —
far denser per-frame data than the first comparison's 37 frames (whose
`bfp` blobs were far smaller; not re-measured here, see that session's
own file listing for comparison). The native scan log shows 4,005
nonzero `"lasere total_points_num"` lines across ~4,000 processed
frames, consistently in the 6,000-8,500-points-per-frame range — real,
sustained laser detection for the whole session, not just isolated
hits.

This lines up directly with this project's own `exposure_sweep_new_setup`
result on the same physical setup (42,224 vertices via
`obscan_scan_get_realtime_model_mesh`): the flatter, better-marker-
coverage setup is a easy case for both the real app and this project's
own bindings, in sharp contrast to the original tall-cup-stack setup
that gave both tools trouble. Further evidence for the conclusion
already drawn in `logs/creality_scan_comparison_20261016/README.md`:
sparse results there were a physical scanning-technique limitation
(marker density/coverage relative to object shape), not a bug in
either implementation.
