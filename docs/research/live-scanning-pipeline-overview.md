# CrealityScan's live scanning pipeline: end-to-end map

A consolidated map of how a captured frame flows from raw sensor data through to the
running reconstruction, tying together this project's two prior reverse-engineering docs
([plane-removal](plane-removal-reverse-engineering.md),
[scan-quality-and-coverage](scan-quality-and-coverage-reverse-engineering.md)) plus new
findings about the ingestion/threading architecture itself. Same method throughout:
manual `nm`/`otool -tV` disassembly of `liborbbec_scan.dylib` (arm64) — Ghidra's full
auto-analysis of this binary consistently exceeds this environment's tool timeout
regardless of request scope (confirmed repeatedly, including for single-function
requests), so none of this used a decompiler.

## The call chain (laser mode, what this project's own tooling and the real app's
marker-mode sessions both use)

```
laser::OBscanLaserCommon::batches_load_frame_datas(
    shared_ptr<LaserScanningContextInterface>, OBScanDataManagement*,
    ProjectFileCache*, name, exposure, mode, callback)
  -- top-level entry, called from the live capture loop per batch of frames.

  -> laser::OBScanLaserFrameProcess::batches_process_frame_data(...)

    -> laser::OBScanLaserFrameProcess::consumer_process_datas(
           callback(points, normals, marker_index_pairs, pose, frame_index),
           poses, RemovePlaneManualInfo-per-frame, OBPointCloud&, OBKdtreeSearch&,
           v3d, v3d, int, int&, bool&, ProgressReporter)
       -- Does NOT process the frame itself. Bundles everything (including the
          per-frame manual-plane-removal info — ties directly into
          plane-removal-reverse-engineering.md's OBScanManualRPConfig_/OBScanPlane_)
          into a std::__bind closure and hands it to
          rs_common::thread_pool_enqueue_task(), returning a std::future<void>.
          Frame processing is asynchronous from the capture thread.

      -> [thread pool worker, off the capture thread] the bound lambda's operator()():
         1. rs_common::ProgressReporter::operator()(float) — progress tick.
         2. Blocks (semaphore wait) on a
            moodycamel::ConcurrentQueue<pair<int, shared_ptr<mkscan::TrackedFrame>>>
            via try_dequeue — waiting for a TrackedFrame object. This is a genuine
            cross-thread hand-off point: something else (the marker-tracking/ICP
            thread) produces TrackedFrame objects onto this exact queue.
         3. rs_common::math::m3d::inverse() — inverts a pose/orientation matrix.
         4. rs_common::OBKdtreeSearch::find_rnn(point, radius, &indices, &distances,
            max_results) — a RADIUS neighbor search (not 1-NN; built on nanoflann
            internally, confirmed via nanoflann::ResultItem<size_t,float> in the call
            graph), used for local point merging/consolidation around each new point.
         5. Touches OrbbecScanLaser::model_mesh_set_detail_scan_area_with_index(...)
            — updates which spatial region(s) of a multi-resolution "detail" mesh this
            frame's data affects.
         6. Invokes the caller-supplied callback with this frame's
            (points, normals, marker_index_pairs, pose, frame_index) — guarded by a
            std::__throw_bad_function_call path, so an unset callback is handled, not
            assumed present.
         7. Enqueues the TrackedFrame onward into the tracking/bundle-adjustment
            subsystem (see below) — laser::OBScanLaserFrameProcess::
            loaded_results_enqueue(pair<int, shared_ptr<TrackedFrame>>&) is the sibling
            producer function on the same queue type.
```

## Where it connects to the bundle-adjustment/pose-validity system

`TrackedFrame` objects (the same type dequeued above) are also consumed by:

- `MKMarkerScanningContext::_enqueue_save_tracked_frame(shared_ptr<TrackedFrame> const&)`
- `mkscan::MkBundleIO::enqueue_tracked_frame_legacy(shared_ptr<TrackedFrame> const&)`

Both are inside the `MKMarkerScanningContext`/`mkscan` namespace that
[scan-quality-and-coverage-reverse-engineering.md §2](scan-quality-and-coverage-reverse-engineering.md)
already identified as the system behind `get_opt_valid_frame_num` — the function that
answers "insufficient data" by counting bundle-adjustment-converged camera poses
(`MarkerGlobalOptResult::cref__xform_tf_valid_poses()`), not raw points. This closes the
loop: **the same `TrackedFrame` objects flowing through per-frame ingestion are what the
bundle adjuster consumes to decide pose validity**, which is what the "insufficient
data" gate and (plausibly) the HUD "Points" counter both ultimately read.

The quality-gate functions from §4 of that doc
(`free_scan::b91078bd`/`compute_cloud_overlap_fast`, gated by `e0a345aa`) are called
from exactly the functions that consume tracked/aligned frames on the other side of this
same hand-off: `GeometryScanningContext::scanning_following`/`scanning_relocation`,
`RGBDScanningContext::scanning_relocation`, and every `free_scan::align_non_kf_*`
variant (geometry, **marker**, RGBD, RGBD-AMAO) — i.e. the code that runs once a
`TrackedFrame`'s candidate pose has been computed and needs to be accepted or rejected
before it's allowed to affect the running reconstruction.

## The full picture, stage by stage

1. **Capture** → raw frame arrives (not traced into this library; happens in the
   sensor/USB layer below `liborbbec_scan`).
2. **Batch ingestion** (`batches_load_frame_datas` → `batches_process_frame_data` →
   `consumer_process_datas`) — bundles frame data + per-frame manual plane-removal
   config, dispatches asynchronously to a thread pool. Does not block the capture
   thread.
3. **Worker processing** (the dequeued lambda) — waits for a `TrackedFrame` (produced by
   tracking/ICP elsewhere), does local radius-neighbor point consolidation, updates the
   relevant detail-mesh region, invokes the per-frame callback, and forwards the
   `TrackedFrame` onward.
4. **Pose tracking / bundle adjustment** (`MKMarkerScanningContext`, `MkBundleIO`,
   `GlobalOptKeyFrame`) — consumes `TrackedFrame`s, computes/refines camera poses.
   `get_opt_valid_frame_num` reports how many of these the optimizer actually trusts —
   the real mechanism behind "insufficient data"
   ([scan-quality-and-coverage-reverse-engineering.md §2](scan-quality-and-coverage-reverse-engineering.md)).
5. **Registration-quality gate** (`b91078bd` / `compute_cloud_overlap_fast` +
   `e0a345aa`) — once a candidate pose exists, scores it (RMS distance + overlap ratio
   against already-fused data) and accepts/rejects it before fusion, identically across
   every tracking mode.
6. **Plane removal** (`obscan_scan_plane_remove`/`turntable_plane_remove`/
   `config_manaul_remove_plane`, `judge_rm_plane_is_need_cut`,
   `point_cloud_remove_plane_below`) — per-point filtering against a tracked or
   manually-specified base plane, happening alongside/within this same frame data.
7. **Weighted voxel fusion** (`GPP::FuseMap::AddPoint`/`ExtractPointCloud`) — accepted,
   plane-filtered points accumulate into a voxel hash grid with a per-point weight;
   final extraction keeps only voxels whose accumulated weight clears a
   per-device `credible_ratio` threshold.

## Marker detection — identified entry point, algorithm internals not yet traced

The actual producer of `TrackedFrame` objects (§3's missing upstream link) starts with
marker *detection* — finding the physical circular dot markers in each camera frame,
before anything can be tracked or bundle-adjusted. Two candidate implementations exist
in this library; only one is live for this project's own scan mode:

- `CircleMarkerDetector` (a full Canny-edge/gradient/local-maxima pipeline:
  `CannyFindLocalMaxima`, `Gradient`, `SmoothGradient`, `ThinAndThreshold`, `MyCanny`) —
  initially assumed to be the live-scan marker detector given the name, but its only
  constructor call site is inside `CaliBoarDetector` ("calibration board detector").
  **It's for the laser calibration tool** (`camera_user_calib`, a separate app
  component), not live scanning — a dead end for this purpose, kept here as a record of
  the wrong turn.
- `mkscan::lib_mkdet::FastMarkerDetector` — constructed from
  `device::LLMKDET_wrapper_impl::wrapper_impl_init(DeviceParameters*, int, bool, int,
  bool, const OBScanModeConfig_&)` ("LLMKDET" = **L**ine-**L**aser **M**ar**K**er
  **DET**ection) and `device::IRMKDET_wrapper_impl::wrapper_impl_init(...)` (the IR
  equivalent). The line-laser variant takes this project's own already-recovered
  `OBScanModeConfig_` struct directly, confirming **this is the real live detector** for
  `OB_SCAN_BLUE_LINE_LASER` + `OB_SCAN_FUSE_MODE_MARKER` — exactly this project's own
  scan configuration. A separate, apparently newer "v5" detection API also exists
  (`mkscan::lib_mkdet::mkv5::MarkerFinder::detect_markers_one_view_aio_impl`) —
  relationship to `FastMarkerDetector` not yet determined (newer replacement? a
  different detection mode?).

### The actual algorithm, stage by stage

`FastMarkerDetector::mk_detect_markers(image1, image2, MkStereoCameraModel const*, ...)`
is a 116-byte thin wrapper — it just forwards to
`MkDetImpl::detect_markers_and_trianglate(...)` ("trianglate" typo preserved from the
vendor), the real 12.7KB implementation. Full line-by-line decoding of a function this
size wasn't attempted (it's the vendor's actual CV algorithm, not architecture glue —
a different kind of work from everything else in this doc); the call-graph skeleton
(every `bl` it makes, cross-referenced back to symbol names) is enough to map its real
stages with confidence, and two of its sub-functions were fully decoded to confirm the
approach:

1. **Per-image ellipse detection** — `mkv5::MarkerFinder::detect_markers_one_view_aio_2506`
   / `..._2603` / `..._2603_wide` / `detect_markers_one_view` (plain fallback) are tried
   in that order on each of the two stereo images independently, each producing a
   `vector<VELLIPSE>`. The numeric suffixes (2506, 2603) read as calibration/resolution
   generation identifiers tried as a cascade — `get_debug_info()` and string
   concatenation calls interleaved between attempts suggest a diagnostic trail recording
   which variant actually found markers, consistent with a "try the preferred one, fall
   back if it comes up short" pattern rather than always running all four.
2. **Stereo correspondence via epipolar geometry** — `_epipolar_matching_fix` (and a
   `_epipolar_matching_fix_collision` sibling, presumably handling the case where more
   than one candidate match satisfies the epipolar constraint) matches ellipses between
   the two `VELLIPSE` lists using the stereo pair's relative rotation/translation
   (`m3d`/`v3d` params) and several double thresholds, producing `vector<pair<int,int>>`
   correspondences.
3. **Triangulation** — `_triangulate_stereo_point`/`_triangulate_stereo_point_optimized`
   turn each matched 2D/2D pair into a 3D point. The "optimized" variant (960 bytes,
   fully decoded) is a **closed-form linear least-squares ray intersection**: builds a
   3×3 normal-equations matrix and right-hand-side vector from the two camera rays
   (accounting for the stereo extrinsics), then solves directly via
   `Eigen::LDLT<Matrix3d>` (a Cholesky variant) — the standard "midpoint method" for
   stereo triangulation, not an iterative or SVD-based approach. Heavily NEON-vectorized
   (paired `fmul.2d`/`fadd.2d`/`faddp.2d` throughout building the matrix), but the
   algorithm itself is textbook, not scan-specific.
4. **Per-marker pose/orientation** — `stereo_circle_pose_estimation_context::check()` —
   a circular marker viewed off-axis projects as an ellipse; the ellipse's eccentricity
   and orientation encode the physical marker's surface normal, not just its center
   position. Not decoded in detail, but the class name and its position in the pipeline
   (right after triangulation, before landmark building) fit recovering per-marker
   orientation this way, not just a 3D point.
5. **Landmark association** — results accumulate into `vector<mk_align::Landmark>`
   ("Landmark" being the standard SLAM term for a tracked 3D feature reused across
   frames), with an `nanoflann`-backed KD-tree search in the mix — almost certainly
   matching newly-triangulated marker positions against already-known landmarks from
   prior frames for ID continuity, rather than treating every frame's markers as new.
6. **Robust refinement** — a `std::nth_element` partial-sort over doubles (the same
   robust-percentile pattern already seen in the overlap-scoring algorithm) plus
   `rs_common::plane_fit` appear late in the function, plausibly filtering outlier
   triangulated points or refining a local marker-cluster plane before the final result
   is returned.

Not yet traced: the exact epipolar-matching distance/threshold formula, the ellipse
detection image-processing internals themselves (Hough-transform-style? contour-based?
not determined), and precisely how the resulting landmark/pose data becomes a
`mkscan::TrackedFrame` object (PnP solve from 2D/3D correspondences is the standard next
step, but not confirmed here) feeding into §2's bundle adjustment.

## What's still open

- The ellipse-detection image-processing internals (`detect_markers_one_view_aio_*`)
  and the exact epipolar-matching threshold formula — see above.
- How triangulated/associated landmarks become a `TrackedFrame` object (plausibly a PnP
  pose solve, not confirmed).
- Where the per-point `weight` passed to `FuseMap::AddPoint` gets computed — plausibly
  somewhere in this worker lambda or its callback, not yet traced to the exact
  instructions.
- Where the per-point `weight` passed to `FuseMap::AddPoint` gets computed — plausibly
  somewhere in this worker lambda or its callback, not yet traced to the exact
  instructions.
- The consumer of §4's CGAL Delaunay triangulation output (built but not used within
  `compute_cloud_overlap_fast` itself).
- `compute_cloud_overlap_fastKDtree` (a third scoring variant, not yet decoded).
- Confirming the "Points" HUD counter hypothesis against the Unity/IL2CPP side (this
  library only covers the native layer).

## What this means for OpenScanner

The real architecture is **not** a single tight loop — it's a producer/consumer
pipeline with a genuine thread-pool hand-off between "frame arrived" and "frame
processed," and a second hand-off from "frame processed" into bundle adjustment. Two
implications for an OpenScanner rewrite:

1. This is worth copying structurally, not avoiding: decoupling capture from
   fusion/scoring via a bounded queue keeps the capture thread responsive regardless of
   how long registration/fusion takes for a given frame — directly relevant to this
   project's own "resource management" goal for live plane removal.
2. The quality gate and the bundle adjuster are two genuinely separate systems reading
   related but distinct data (per-frame RMS/overlap score vs. accumulated pose-graph
   validity) — a real replacement needs both, not just one unified "frame is good"
   check. Collapsing them into a single heuristic is a plausible way the real app's own
   "insufficient data" ends up feeling opaque: a frame can fail either gate for
   different reasons, but the user only ever sees one undifferentiated failure message.
