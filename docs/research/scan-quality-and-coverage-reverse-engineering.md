# CrealityScan's point/frame/scan quality pipeline: reverse-engineered

Research notes on how CrealityScan decides per-point quality, per-frame acceptance, and
overall "do we have enough data yet" — reverse-engineered from `liborbbec_scan.dylib`
(arm64) via manual `nm`/`otool -tV` disassembly, same constraints and method as
[plane-removal-reverse-engineering.md](plane-removal-reverse-engineering.md) (Ghidra's
full auto-analysis of this binary still exceeds this environment's tool timeout).
Motivated by: "this pipeline from creality is going to need an overhaul" — understanding
the real mechanism before replacing it.

There isn't one quality score. There are (at least) three separate, stacked layers:

## 1. Per-frame acceptance gate — is this frame even worth processing?

`free_scan::new_frame_valid(const Isometry3d& delta_pose, float rotation_threshold_deg,
float translation_threshold) -> bool`, fully decoded from disassembly (pure scalar FP,
no calls out except `atan`):

```c
bool new_frame_valid(Isometry3d delta_pose, float rotation_threshold_deg,
                      float translation_threshold) {
    double angle_deg = quaternion_angle_degrees(delta_pose.get_rotation());  // atan-based
    if (fabs(angle_deg) > rotation_threshold_deg) return true;
    double translation = length(delta_pose.translation);  // sqrt(x²+y²+z²)
    return translation > translation_threshold;
}
```

A standard keyframe gate: a frame only gets fed into the rest of the pipeline if the
scanner has rotated or moved far enough since the reference pose. `free_scan::
motion_prediction_valid(quatd, v3d, float, float)` is the same shape applied to an
IMU-predicted pose instead of a measured one — almost certainly deciding whether to
trust the IMU's motion prediction for tracking or fall back to vision-only.

## 2. "Insufficient data" — not a point count, a bundle-adjustment pose count

`laser::OBscanLaserCommon::get_opt_valid_frame_num(const OBScanModeConfig_& config,
LaserScanningContextInterface* ctx, int& out_count) -> ObScanStatus` branches entirely
on `config.fuse_mode` (confirmed against this project's own `obscan_api.h`:
`OB_SCAN_FUSE_MODE_MARKERLESS = 3`):

- **`fuse_mode == MARKERLESS (3)`**: counts entries in
  `LaserTextureScanning::get_optimization_recored_valid_frames_result()` (a plain vector,
  16 bytes/element).
- **Every other mode, including `MARKER (2)`** — what this project's own tooling and
  every comparison session against the real app has used — goes through a completely
  different path: `MKMarkerScanningContext::get_optimization_results()` →
  `MarkerGlobalOptResult::cref__xform_tf_valid_poses()`, counting entries in a vector of
  128-byte pose/transform records (the error string guarding this path literally reads
  `"the ba result is nullptr"` — **ba = bundle adjustment**).

This is the real mechanism: for marker-tracked scanning, "do we have enough data" is
answered by **how many camera poses successfully converged in a global
marker-based bundle adjustment**, not by any point or vertex count. A pose that doesn't
converge (because markers visible in that frame are too few, too close together, or
inconsistent with the rest of the pose graph) simply isn't counted, regardless of how
much raw depth/laser data was captured in that frame.

This directly explains this project's own earlier empirical finding
(`logs/creality_scan_comparison_20261016/`): a tall, narrow object with markers mostly
on a separate flat mat produced "Frames 2,991 / Points 90" and a hard "insufficient
data" block in the real app. It is very plausible that the HUD's **"Points" counter is
not a point count at all, but this same valid-pose count** (or something close to it) —
its values (90, 104, 139, 149 across different sessions) were always far too small and
grew far too slowly to be literal 3D points, but line up well with "number of frames
whose camera pose the bundle adjuster actually trusted." Not yet confirmed against a
disassembly of the HUD-update code itself (would need the Unity-side IL2CPP code, not
this native library) — a reasonable next step if this matters.

Practical implication: a marker layout that gives every frame a *visible* marker but a
*geometrically weak* one (e.g. markers clustered in a small region of the frame,
low-parallax between consecutive views) can produce plenty of raw frames and plenty of
marker *detections* while still starving the bundle adjuster of frames it's willing to
count as valid — matching the "doesn't work at all" experience even when markers are
technically being seen.

## 3. Per-point/voxel quality — weighted fusion, thresholded at extraction

`GPP::FuseMap` is a voxel hash-grid fusion map (classic TSDF-style accumulation, not
specific to any one scan mode):

```c
void FuseMap::Init(double voxel_size, const Vector3& origin);
void FuseMap::AddPoint(const Vector3& point, double weight, const Vector3* normal,
                        const Vector3* color, const double* /*?*/, int, const int*,
                        const unsigned char*);
size_t FuseMap::HashValue(int x, int y, int z) const;  // voxel coordinate hash
bool FuseMap::ExtractPointCloud(IPointCloud* out, std::vector<double>* weights,
                                 std::vector<double>* /*?*/, int /*?*/, int*** /*?*/,
                                 double credible_ratio) const;
```

Every observed point is accumulated into its voxel **with a weight**, across however
many frames see that voxel — not kept or discarded on the spot. Final output extraction
(`ExtractPointCloud`) only keeps voxels whose accumulated weight clears a threshold
derived from `credible_ratio` — confirmed by the guard strings this function (and its
siblings `FusePointCloud`/`GridFusePointCloud`) all share: `"ExtractCloudByWeight:
invalid credibleRatio"`, `"FuseMap::ExtractPointCloud: invalid credibleRatio"`,
`"insufficient valid points"`.

`credible_ratio` itself isn't a fixed constant — `speckle::get_speckle_credible_ratio
(OBScanDeviceType, float, int) -> float` looks it up from per-device-model rule tables
(`raptor_rules`, `otter_rules`, `p1_lite_object_{medium,big}_rules`,
`m1_ultra_object_{small,medium,big}_rules` — all found as local symbols, not yet
disassembled). This is a sound design in principle (don't hard-code one confidence
threshold across every camera/lens combination the company ships) — whether the actual
per-device values are well-tuned is a separate, unverified question. Real captured
sessions this project has logged used `credible_ratio` values of 0.3, 0.2, and 0.227
across different scans, consistent with it being computed per-session rather than fixed.

Where the per-point `weight` passed into `AddPoint` itself comes from (sensor
confidence? angle-of-incidence? distance-from-camera falloff? something from the
registration-quality context in §4?) is not yet traced — the call sites that build it
would be in the per-frame laser processing code (`OrbbecScanLaser::
consumer_process_datas`, found via `nm` in the plane-removal doc's research but not
disassembled), not in `FuseMap` itself.

## 4. Registration/overlap quality — a separate scoring context, not yet fully traced

`free_scan::PointCloudRegistrationQualityContext` is a distinct quality-scoring
structure with its own pipeline:

```
quality_params_init(SpeckleTotalParams const&, PointCloudRegistrationQualityContext&)
build_point_cloud_registration_quality_context_fast(IcpPointCloudContext const&,
    IcpPointCloudResult const&, SpeckleTotalParams const&,
    PointCloudRegistrationQualityContext&)
build_point_cloud_registration_qualityKdtreefast(..., slam::LKDtree const*)
compute_cloud_overlap_fast(PointCloudRegistrationQualityContext&, SpeckleTotalParams const&)
compute_cloud_overlap_fastKDtree(..., slam::LKDtree const*)
```

`compute_cloud_overlap_fast` computing *overlap* between a new frame's cloud and the
existing fused data is exactly the kind of signal a real "where does the scan still need
coverage" feature would be built on — low overlap with anything already captured means
genuinely new territory; high overlap means redundant data. Notably this family's
public-facing helper functions have **hash-like, non-descriptive names**
(`free_scan::e0a345aa`, `b91078bd`, `ce17482b`, `be7dbca4` — all taking
`PointCloudRegistrationQualityContext`), unlike almost everything else in this library,
which keeps full readable C++ names throughout. That's a real signal in itself: this is
the one area of the pipeline Creality appears to have deliberately obscured, which lines
up with it being core, differentiating IP — and makes it the most expensive part of this
to fully reverse-engineer (would need actual decompilation of 4+ stripped-name
functions, not just signature recovery from an unmangled name). Not attempted yet.

Also found but not traced: the literal string `"Maximum effective coverage area:
%.2f%%"` — a percentage coverage metric gets computed and logged somewhere in this
library; the function that produces it hasn't been located yet (the string's cross-refs
weren't checked — would need either Ghidra's xref graph, once its timeout issue is
worked around for this binary, or a manual scan of call sites near known coverage/overlap
functions).

## 5. What this means for OpenScanner's own pipeline

Three separable concerns, matching the three layers above, each independently
replaceable:

1. **Frame gating** (§1) is simple and worth copying outright: reject frames whose pose
   hasn't moved enough since the last accepted one. Cheap, well-understood, no reason to
   reinvent differently.
2. **"Do we have enough" / where coverage is weak** (§2, §4) is the part most worth
   overhauling. The real app's marker-mode answer (bundle-adjustment pose validity
   count) is reasonable in concept but opaque to the user — it fails as "insufficient
   data" with no indication of *where* coverage is weak or *why* a frame's pose didn't
   count. A spatial overlap/coverage map (built from something like §4's overlap
   computation, scored per-region rather than only as a single running total) would be a
   real improvement and matches what the user is after: showing *where* the scan needs
   more data, not just refusing to finish with no explanation.
3. **Per-point/voxel confidence** (§3) — weighted TSDF-style fusion with a
   credibility-threshold extraction is a sound, standard approach already; the open
   question is calibration (`credible_ratio`, the per-point weight itself), not the
   architecture. Worth keeping the shape, revisiting the numbers.

Not yet done: tracing the actual per-point weight computation (§3's open question),
confirming the "Points" HUD counter = bundle-adjustment valid-pose-count hypothesis
(§2) against the Unity/IL2CPP side, and any disassembly of the obscured
`PointCloudRegistrationQualityContext` helper functions (§4).
