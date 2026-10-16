# Real CrealityScan.app session, for direct comparison

Project name: `1st_comp_to_open3dscanner` (named by the user for this
purpose). Scanner left in the same physical position/marker layout as
this session's own `tools/live_capture_test` runs, right before the
scanner was moved — intended as a like-for-like comparison of what the
real app produces vs. our own clean-room bindings against the same
hardware/scene.

## What this folder contains

- `physical_setup/` — three reference photos of the actual physical
  scene from the scanner's own point of view: a stack of red plastic
  cups (each printed with a dot pattern of its own) standing on a
  wooden surface, surrounded by a printed reference-marker mat (small
  black-ringed circular dots in a grid). This is the scene behind every
  number below and in `logs/stop_abi_fix_validation/` from the same
  session.
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

### Why, looking at `physical_setup/`

The reference-marker mat on the table has decent density (a regular
grid of small dot markers), but the actual scan target — a stack of
tall, narrow, cylindrical plastic cups — has very little of its *own*
trackable surface: a cylinder presents a constantly-changing silhouette
as the scanner moves around it, and the mat's markers are mostly
beside/behind the cups rather than on or near the object itself. Marker-
based multi-view registration needs several shared markers visible and
stable across consecutive frames; a tall narrow object sitting apart
from a flat mat of markers is a comparatively hard case for that,
independent of which implementation (this project's or Creality's own)
is doing the tracking. A flatter, wider object sitting directly on/
surrounded by markers, or markers placed on the object itself, would be
a more favorable next test case for either tool.

### Correction from the native scan log: laser extraction was starved too

Counting the per-frame `lasere total_points_num` lines in
`native_logs/scan_log_20261016-024738.txt` (done later, for
`docs/research/pipeline-quality-assessment.md`) adds something the
conclusion above did not have:

- The first scan attempt (`BTN_STATUS_START` at 02:48:28, deleted at
  02:48:54) produced **zero laser points on all 1,916 frames** while the
  laser was on. The laser light type stayed at 3 for the whole attempt
  instead of alternating 3/4 as it does in preview and in every healthy
  run. Nothing from that attempt ever reached fusion.
- The restarted attempt (02:49:01 onward) extracted about 1,500 points
  per frame for its first minute and about 3,000 for its second. The
  clean session in `logs/creality_scan_comparison2_20261016/` averaged
  about 7,500 on the same scanner. This session's saved
  `scanParamConfig.dat` ends with `IrAutoMode: false` and
  `IrExpAndLaserValue: 270`, whereas the config loaded at 02:48:15 had
  auto mode on at 500, so the exposure slider was changed in the UI at
  some point the native log does not record.

So the bundle adjuster here was fed two to five times fewer points per
frame than in the clean session, after a first attempt that fed it
nothing. Marker density relative to the object is probably still part
of it, but "a physical scanning-technique limitation shared by both
tools" overstates what this log supports: exposure and laser state were
at least as different between the two sessions as the marker layout was.
