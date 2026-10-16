# CrealityScan's scanning pipeline: quality assessment and path forward

A stage-by-stage verdict on the vendor's live scanning data pipeline, drawn from this
project's reverse-engineering docs
([live pipeline overview](live-scanning-pipeline-overview.md),
[quality and coverage](scan-quality-and-coverage-reverse-engineering.md),
[plane removal](plane-removal-reverse-engineering.md)) plus a new source the earlier
docs did not use: the vendor SDK's own per-frame scan logs from the two real-app
comparison sessions (`logs/creality_scan_comparison_20261016/`,
`logs/creality_scan_comparison2_20261016/`) and from this project's own runs. Each stage
gets one of three calls: **copy** (sound, reproduce it as-is), **redesign** (the
algorithm is fine but the way it is wrapped or reported is not), or **reimplement**
(write our own, either because the vendor's is broken or because it is small and
owning it buys diagnostics the vendor hides).

## Verdict in one paragraph

The vendor pipeline is architecturally sound and mostly textbook. Its weak points are
not the heavy numerics but the decision and feedback layers around them: a per-frame
quality gate that computes per-point residuals and then throws them away in favour of
one pass/fail scalar, a plane-removal decision function with a nonsensical fallback
branch, and an "insufficient data" verdict with no per-pose reason behind it. The parts
that would be hardest to reimplement, laser-line extraction and thermally compensated
stereo calibration, are also the parts the logs show working well. So: reimplement the
small, broken, or opaque logic; redesign the feedback layer; keep the sensor-side math
behind the SDK until a decision to drop OBScan entirely is made on its own merits.

## 1. What the vendor logs say about pipeline health

The disassembly docs describe mechanism. The scan logs quantify behaviour. Counting
per-frame log lines in the two comparison sessions (the real CrealityScan app, same
scanner, same SDK build 3.5.3):

| | Good session (flat setup) | Failed session (cup stack) |
|---|---|---|
| Laser frames extracted while scanning | 4,005 | 5,729 |
| Frames dropped while scanning | 0 | 0 |
| Frames with zero laser points | 30 | 1,870 |
| Mean points per frame, all frames | 7,522 | 1,408 |
| Mean points per frame, nonzero frames | 7,580 | 2,090 |
| Keyframes persisted (`save task`) | 38 | 37 |
| IR exposure | auto, 500 | manual, 270 (per saved config) |

Four findings that the existing docs either lack or get partly wrong:

### 1.1 "processBLAEFrame skip!" and "queue is full" are the paused state, not backpressure

Every one of the 4,662 skip lines and 4,634 queue-full lines in the good session falls
after `obscan_scan_pause`, when the laser light type reads 255 (laser off). Bucketing by
10-second window makes it unambiguous: while scanning, processed equals arrived in every
bucket, with zero skips and zero queue-full warnings; the moment the scan pauses,
processed drops to zero and every arriving frame is skipped. Same shape in the failed
session, where the two skip bursts align with the user's delete at 02:48:54 and the
pause at 02:50:40.

During active scanning the SDK extracts laser points from 100 percent of arriving
frames at 35 to 44 Hz against a 46 Hz IR stream profile (1920×1200). The per-frame
exposure step ("blae run", which from `updateBLAEParams` and the light-type outputs
reads as the blue-laser auto-exposure controller) completes in under a millisecond, and
each laser extraction lands within about 30 ms of the previous one, on its own thread.
The cross-line mode alternates light type 3 and 4 frame by frame; type 1 images yield
about 8,500 points and type 2 about 6,500. A batch boundary (`blae_process finish`)
lands every 14 to 15 frames. Keyframes persist at roughly one every three seconds.

The ingestion and threading design the overview doc describes is, on this evidence,
genuinely real-time on an M1 and not where the problems are.

### 1.2 The failed session was starved of laser points, not only of marker geometry

`logs/creality_scan_comparison_20261016/README.md` attributes that session's
"insufficient data" outcome to marker layout relative to a tall narrow object. The
scan log shows something the README did not have:

- The first scan attempt (start 02:48:28, deleted by the user at 02:48:54) produced
  **zero laser points on all 1,916 frames** while the laser was on. The light type
  stayed at 3 for the whole attempt instead of alternating 3/4 as it did in preview
  and in every healthy run. Whatever the cause (exposure, laser mode, or a device state
  that the delete-and-restart cleared), nothing reached fusion from that attempt.
- The restarted attempt (02:49:01 onward) extracted about 1,500 points per frame for
  its first minute and about 3,000 for its second, against 7,500 in the good session.
  No exposure call is logged at the transition; the app's saved config for the session
  ends with IR auto mode off and value 270, whereas the config it loaded at 02:48:15
  had auto mode on at 500, so the slider was changed in the UI at some point the
  native log does not show.

So the bundle adjuster in that session was fed two to five times fewer points per
frame than in the good one, after a first attempt that fed it nothing. Marker geometry
was likely still a factor, but the quality doc's "insufficient data is a bundle-
adjustment pose count, not a point count" story needs this caveat: the pose count can
also be low because the laser extraction upstream was starved.

### 1.3 The real app never polls for the live cloud

In the good session's native log the real app calls `obscan_scan_get_raw_clouds`,
`obscan_scan_get_global_pose`, `obscan_scan_get_markers` and
`obscan_scan_get_optimization_mesh` **exactly once each, at scan creation**, and never
again during the two minutes of scanning. Live frames reach it through the data-message
listener (`OB_SCAN_MESSAGE_ID_FRAME_READY` and the per-stream image messages already
recovered in `obscan_api.h`). This is the most likely explanation for this project's
standing "raw cloud always returns zero points during scanning" finding: the getter is
not the live path in the vendor's own app either.

### 1.4 Thermal compensation of calibration happens inside the SDK

Ten times per session the log prints the stereo intrinsics being recomputed from live
sensor temperatures (IR left/right and projector at about 32 °C) against the
calibration-time temperatures (about 35 °C), with the recomputed baseline clamped to a
`tx_min`/`tx_max` band. This model, and the per-device rule tables that
`speckle::get_speckle_credible_ratio` reads, are the two pieces of the pipeline that
depend on vendor calibration data rather than on an algorithm one can look up.

### 1.5 Our own tooling drives the SDK differently from the real app

In this project's runs (`logs/exposure_sweep_new_setup_20261016/`,
`logs/stop_abi_fix_validation/`) the IR, depth and frame queues overflow continuously
*during* scanning: about 3,000 queue-full warnings in a 30-second run, plus a `frame
queue is full` warning the real app never emits. Laser extraction is unaffected
(processed still equals arrived), but it means we register the data listener and then
never consume what it delivers, so the SDK copies every preview frame into a queue
nobody drains. The USB layer below is clean in both: a handful of frames dropped for
size mismatch, nothing else.

## 2. Stage-by-stage calls

| Stage | What the vendor does | Quality | Call |
|---|---|---|---|
| USB capture, stream profiles | Open-source OrbbecSDK | Clean | **Copy** (bind as-is) |
| Laser-line extraction, stereo triangulation, thermal calibration | Per-frame, GPU-assisted, temperature-compensated intrinsics | High yield, real-time | **Keep behind the SDK**, reimplement last |
| Marker detection | Tiled hand-written blob scan, image-moments ellipse fit, epipolar matching, LDLT midpoint triangulation | Textbook and fast; moments fit is weak under partial occlusion | **Reimplement** (modest size) |
| Keyframe gate (`new_frame_valid`) | Rotation/translation delta thresholds | Trivial, correct | **Copy** |
| Thread pools and bounded-queue hand-off | Capture decoupled from processing via a thread pool and a concurrent queue | Sound | **Copy structurally** |
| Registration-quality gate (`b91078bd`, `compute_cloud_overlap_fast`, `e0a345aa`) | Robust-percentile RMS plus overlap ratio, thresholded | Algorithm fine; output is one scalar, per-point residuals discarded | **Redesign** |
| Bundle adjustment and "insufficient data" | Marker-based global optimisation; valid-pose count | Concept fine; no per-pose reason, HUD counter unexplained | **Redesign the reporting; reimplement the solver** |
| Plane removal (`judge_rm_plane_is_need_cut`) | Signed-distance test, then a Z-axis fallback that can cut real geometry | Broken decision logic on a correct filter | **Reimplement outright** |
| Weighted voxel fusion (`GPP::FuseMap`) | Hash-grid accumulation, per-device credible ratio at extraction | Standard; per-point weight source untraced | **Reimplement** on the same shape |
| Texture, feature matching, AI mesh (SuperPoint on ncnn/Vulkan) | Learned feature network, built even on teardown | Source of the destroy-path crash and hang | **Replace** with our own mesh stack (already planned) |

The reasoning behind the three calls that are not obvious from the table:

**Plane removal is the best value per effort.** The bulk filter
(`point_cloud_remove_plane_below`) is a half-space test anyone can write in a few
lines. The broken part is a twenty-line decision function. A RANSAC plane fit over the
accumulating cloud plus a plain signed-distance threshold, applied per frame before
points reach fusion, delivers the live resource-management goal from the plane-removal
doc and sidesteps the vendor's fallback branch entirely. No ABI binding of the vendor's
own four plane functions is needed for this path, though binding them stays useful as
an A/B reference.

**The quality gate should be redesigned, not copied.** The nearest-neighbour scan in
`b91078bd` already produces a squared distance for every reference point against the
newest accepted frame. Keeping that per-point result instead of collapsing it to one
ratio yields an under-covered point set for free, paintable onto the live cloud as a
heatmap. Same KD-tree query, same cost, materially better feedback than one
undifferentiated "insufficient data" message. The two-threshold structure (loose for
RMS, tight for inlier count) and the percentile trick are worth keeping verbatim.

**Reimplementing tracking is a medium-large job and should come after the above.**
Detection, landmark association, PnP, and bundle adjustment are standard SLAM
components, and Ceres Solver is BSD-licensed, so there is no licensing obstacle. The
payoff is per-pose diagnostics (which markers were seen, why a pose was rejected),
which is exactly what the vendor hides. But those diagnostics only help once the
feedback layer exists to display them, so this is sequenced after the heatmap work.

## 3. Recommended order

1. **Fix our own SDK usage first.** Consume `OB_SCAN_MESSAGE_ID_FRAME_READY` and the
   per-stream image messages from the data listener instead of polling
   `obscan_scan_get_raw_cloud`; drain the IR and depth queues so they stop overflowing;
   and align voxel sizes with what the real app uses. `tools/scan_server/main.cpp`
   currently sets `voxel_size = 0.1` and `detail_voxel_size = 0.05` against the real
   app's 0.5 and 0.25 (both sessions' `scanParamConfig.dat`), which is 125 times the
   voxel count at the coarse level for no demonstrated benefit. None of this is a
   vendor problem; all of it is cheap.
2. **Reimplement live plane removal.** Small, self-contained, and it retires a
   known-bad vendor path. See [plane-removal-reverse-engineering.md §5](plane-removal-reverse-engineering.md).
3. **Build the coverage heatmap on top of vendor output.** `obscan_scan_get_global_pose`
   and `obscan_scan_get_markers` exist and the real app calls them; binding them lets
   our UI compute and paint under-covered regions from the vendor's own mesh and poses
   before any tracking code of our own exists.
4. **Reimplement marker detection and bundle adjustment** as the first owned tracking
   piece, with per-pose rejection reasons surfaced to the UI. Replace the moments-based
   ellipse fit with a proper conic fit while there.
5. **Leave laser extraction and calibration in the SDK** until the "drop OBScan
   entirely" decision (README, "Possible future direction") is made on its own merits.
   If it is made, this is the piece that needs a calibration workflow of its own, not
   just an algorithm.

## 4. Open questions this assessment did not close

- What `dgg device: 104`, logged after every laser extraction, refers to (a GPU
  dispatch tag is the obvious guess; `dgg_deinit_gpu` appears at teardown).
- What the `blae_process finish` batch boundary every 14 to 15 frames feeds (bundle
  input, exposure statistics, or keyframe selection).
- Why the failed session's first attempt stayed on a single laser line type with zero
  extracted points, and what the user changed before the restart.
- Whether the HUD "Points" counter is the bundle-adjustment valid-pose count (the good
  session persisted 38 keyframes while the HUD showed 149, so it is not the keyframe
  count either).
- Where `FuseMap::AddPoint`'s per-point weight is computed (still open from the quality
  doc).
