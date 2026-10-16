# Real CrealityScan.app session, for direct comparison

Project name: `1st_comp_to_open3dscanner` (named by the user for this
purpose). Scanner left in the same physical position/marker layout as
this session's own `tools/live_capture_test` runs, right before the
scanner was moved — intended as a like-for-like comparison of what the
real app produces vs. our own clean-room bindings against the same
hardware/scene.

## What this folder contains

- `screenshots/` — three screenshots taken during the live session:
  1. `1_live_scan_view.webp` — the live point-cloud/marker view mid-scan.
  2. `2_scan_settings_panel.png` — the exact scan settings in use.
  3. `3_hud_and_ir_camera.webp` — the live HUD (frame/point/marker
     counts) plus the raw IR camera feed with marker detection
     highlighted (red/white circles overlaid on the actual sensor
     image) — this is the clearest direct evidence of what "marker
     detected" looks like at the vendor SDK level, something neither
     session has found a dedicated log line for.
- `project_metadata/` — `scanParamConfig.dat` (JSON — the real session's
  exact mode config), `project.obp`, and a file listing
  (`obscan_database_file_listing.txt`) of the project's raw SQLite scan
  database (`resources.obscan`, ~95MB — **not copied into this repo**,
  right at GitHub's 100MB single-file limit and mostly large per-frame
  binary blobs; it remains at
  `~/Library/CrealityScan/Projects/1st_comp_to_open3dscanner/` locally
  if deeper byte-level inspection is ever needed. Its `mode_config.bat`
  blob, checked, is the app's own internal database serialization
  format — not the live `obscan_*` C ABI this project binds against, so
  it isn't useful for validating `obscan_api.h`).
- `native_logs/` — the complete vendor log directory for this session
  (`scan_log_*.txt`, `ScannerStreamSDK.log`, `app.log`, `device_info.json`,
  `system_info.txt`, `Native/*.log`), copied as-is from
  `~/Library/CrealityScan/Logs/20261016024738/`.

## Session settings (from the Scan Settings panel + scanParamConfig.dat)

| Setting | Value |
|---|---|
| Projector | Blue Laser Mode |
| Tracking Mode | With Markers |
| Scan Mode | Pointcloud |
| Real-time Resolution | 0.5mm (`VoxelSizeProperty: 0.5`) |
| Laser Mode | Cross lines |
| Exclude Flat Base | OFF |
| Color Mapping | OFF |
| Scanning Range | 200-550mm |

Matches this project's own `MARKER` / `BLUE_LINE_LASER` / `CROSS` /
`NO_TEXTURE` config choices. One real difference found: this session's
`VoxelSizeProperty` is **0.5**, not the 0.1 this project adopted as
"ground truth" from an older (2026-10-14) session's `scanParamConfig.dat`
— confirming that value isn't fixed across sessions and 0.5 may be
worth trying in `live_capture_test` too.

## Result: the real app also struggled with this exact marker layout

From the HUD (`3_hud_and_ir_camera.webp`): **Frames 2,991, Points 90,
Markers 28**, 32°C, 19fps. The app refused to let the scan finish
("insufficient data"). Only 37 of the 2,991 frames were actually
persisted to the scan database (`bfi_0..36` / `bfp_0..36` in the file
listing) — the rest never contributed usable tracked data.

This is a real, useful data point for this project's own investigation:
even the vendor's own app, with its own calibration and exposure
auto-tuning, could not reliably accumulate a usable point count from
this specific marker layout. That lines up with this session's own
`live_capture_test` findings earlier the same night — real per-frame
laser detection working fine, but very little making it into a final
accumulated model — and points at marker density/coverage for
multi-view registration as a physical scanning-technique limitation
shared by both tools, not a bug specific to this project's bindings.
(Earlier the same session, a *different* physical setup did get this
project's own tooling to 5900+ real mesh vertices — see
`logs/stop_abi_fix_validation/` — so the pipeline itself is confirmed
capable of producing real data; this particular marker layout was
evidently a harder case for both implementations.)
