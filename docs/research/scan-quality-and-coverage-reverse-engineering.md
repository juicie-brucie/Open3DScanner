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

## 4. Registration/overlap quality — fully decomposed

`free_scan::PointCloudRegistrationQualityContext` is a distinct quality-scoring
structure, built/consumed by its own small pipeline:

```
quality_params_init(SpeckleTotalParams const&, PointCloudRegistrationQualityContext&)
build_point_cloud_registration_quality_context_fast(IcpPointCloudContext const&,
    IcpPointCloudResult const&, SpeckleTotalParams const&,
    PointCloudRegistrationQualityContext&)
build_point_cloud_registration_qualityKdtreefast(..., slam::LKDtree const*)
compute_cloud_overlap_fast(PointCloudRegistrationQualityContext&, SpeckleTotalParams const&)
compute_cloud_overlap_fastKDtree(..., slam::LKDtree const*)
```

Four of this family's helper functions have **hash-like, non-descriptive names**
(`free_scan::e0a345aa`, `b91078bd`, `ce17482b`, `be7dbca4`), unlike almost everything
else in this library, which keeps full readable C++ names throughout — the one area of
the pipeline Creality appears to have deliberately obscured, consistent with it being
core, differentiating IP. Despite the obscured names, full manual decompilation (no
Ghidra — its full-binary auto-analysis consistently exceeds this environment's tool
timeout regardless of how narrow the request is, confirmed by asking it to analyze a
single function and still hitting the same 330s wall; it re-runs whole-binary analysis
on every call with no caching between calls) was tractable, since the actual algorithms
turned out to be plain scalar/SIMD math and standard library calls, not control-flow
obfuscation.

### `ce17482b` — builds the context from an ICP result (188 bytes, fully decoded)

```c
void ce17482b(const IcpPointCloudContext& ctx, const IcpPointCloudResult& result,
              const SpeckleTotalParams& params, PointCloudRegistrationQualityContext& out) {
    // Copies 4 pose/transform-sized (24-byte) records verbatim from ctx into out.
    memcpy(&out, &ctx.field_0xb0, 0x60);  // four 24-byte records, field-for-field
    // Copies 7 doubles from result straight across...
    for (int i = 0; i < 7; i++) out.field[0x60 + i*8] = result.field[i*8];
    // ...but the 8th is validated first.
    double last = result.field_0x38;
    if (isfinite(last) && last <= (double)params.field_0xf4) {
        out.field_0x60_plus_0x38 = last;      // accepted — proceed to score it for real
        b91078bd(out, params);                 // tail call
    } else {
        out.rms_distance = 1.0;                // degenerate/"no overlap possible" sentinel
        out.overlap_ratio = 0.0;
        out.inlier_count = 0;
    }
}
```

The 8th `IcpPointCloudResult` field (likely a final ICP residual/error) gates whether
registration-quality scoring runs at all for this frame — if the ICP itself reported a
result that's non-finite or worse than `params.field_0xf4` (a configured max-error
threshold), the frame gets a hardcoded "failed" quality record instead of ever reaching
the real scoring function below. `be7dbca4` (240 bytes, same shape, operating on a
`PointCloudRelocationContext`/`RelocationResult` pair instead) is the same pattern
applied after a *relocation* event (tracking lost and recovered) rather than ordinary
ICP — not independently decoded, but confidently the same structure by symmetry.

### `b91078bd` — the actual overlap/RMS-distance computation (2.1KB, decoded to algorithm level)

This is the function that fills in the three output fields (`out+0x98`, `out+0xa0`,
`out+0xa8`) that everything else in this family reads or gates on:

```c
void b91078bd(PointCloudRegistrationQualityContext& ctx, const SpeckleTotalParams& params) {
    if (ctx.ref_cloud_count == 0 || ctx.new_cloud_count == 0) {
        ctx.rms_distance = SENTINEL;  // a fixed literal-pool constant; "no data" case
        return;
    }

    // 1. Axis-aligned bounding box of the REFERENCE (already-fused) cloud.
    Vector3 lo = +inf, hi = -inf;
    for each point in reference_cloud: lo = min(lo, point); hi = max(hi, point);
    lo -= 0.1; hi += 0.1;  // pad by a fixed 0.1-unit tolerance margin

    // 2. Cull the NEW frame's cloud down to points inside that padded box —
    //    cheap spatial pre-filter before the expensive nearest-neighbor work.
    OBPointCloud culled;
    for each point in new_cloud: if (point inside [lo, hi]) culled.push_back(point);

    // 3. Build a KD-tree over just the culled subset.
    OBKdtreeSearch tree; tree.build(culled);

    // 4. For every point in the REFERENCE cloud, find its nearest neighbor in the
    //    culled new-frame cloud and classify it by two separate squared-distance
    //    thresholds (both read from SpeckleTotalParams, i.e. independently tunable):
    std::vector<double> matched_distances;
    int inlier_count = 0;
    for each point in reference_cloud {
        double sq_dist = tree.find_knn(point, k=1);
        if (sq_dist < loose_threshold_sq)  matched_distances.push_back(sq_dist);
        if (sq_dist < tight_threshold_sq)  inlier_count++;
    }

    // 5. Robust RMS: only the closest `percentile_fraction` of matches (a
    //    SpeckleTotalParams field) count, via std::nth_element partial sort —
    //    this throws out the worst matches instead of averaging over everything,
    //    so genuinely-new geometry (which has no good match yet, by definition)
    //    doesn't drag the registration-quality estimate down.
    size_t k = (size_t)(matched_distances.size() * percentile_fraction);
    std::nth_element(matched_distances.begin(), matched_distances.begin()+k, matched_distances.end());
    double mean_sq = average(matched_distances.begin(), matched_distances.begin()+k);

    ctx.rms_distance  = sqrt(mean_sq);                                    // out+0x98
    ctx.overlap_ratio = (double)inlier_count
                        / (double)min(ctx.new_cloud_count, ctx.ref_cloud_count); // out+0xa0
    ctx.inlier_count  = inlier_count;                                     // out+0xa8
}
```

This retroactively corrects the read on `e0a345aa` (60 bytes, decoded first, before
`out+0x98`/`out+0xa0`'s real meaning was known): it isn't a spatial range-overlap test —
it's a **two-sided quality gate** over these exact two output fields:

```c
bool e0a345aa(const PointCloudRegistrationQualityContext& ctx, double min_overlap, double max_rms) {
    return isfinite(ctx.rms_distance) && isfinite(ctx.overlap_ratio)
        && ctx.rms_distance <= max_rms && ctx.overlap_ratio >= min_overlap;
}
```
i.e. "is this frame's registration good enough to trust" — RMS distance below a ceiling
*and* overlap ratio above a floor, both caller-supplied. This is almost certainly the
actual accept/reject check sitting downstream of `b91078bd`'s scoring (not yet traced to
its call site — would need the caller of `e0a345aa` itself, not yet located).

### `compute_cloud_overlap_fast` — a *separate*, grid-based algorithm, not a wrapper

Initially assumed (wrongly) to just be a thin dispatcher into `b91078bd` given the
shared context type. It is not: its prologue operates on completely different context
fields (`ctx+0x128`, a count; `ctx+0xd0`, two packed `int32`s multiplied together —
`smull x22, w9, w8` — i.e. a **grid width × height**; `ctx+0xf8`/`ctx+0x110`, two more
buffer pointers) and its main loop is manually unrolled ×8 for SIMD, the signature of a
dense per-cell loop over a 2D grid rather than a sparse KD-tree point search. This reads
as a second, independent overlap estimator working over a rasterized grid/height-map
representation instead of raw 3D points — plausibly a cheaper, real-time-friendly
approximation used for live UI feedback (a coverage percentage/heatmap), while
`b91078bd`'s point-based KD-tree version is the more precise one used to gate whether a
frame's registration is trustworthy enough to fuse at all. Not decoded further — this is
a genuinely separate ~3KB function and a distinct follow-up task from the point-based
algorithm above.

Also found but not traced: the literal string `"Maximum effective coverage area:
%.2f%%"` — a percentage coverage metric gets computed and logged somewhere in this
library, plausibly downstream of `compute_cloud_overlap_fast`'s grid-based result given
both are naturally area/grid-shaped; the producing function hasn't been located (would
need the string's cross-refs, which neither `nm`/`strings` nor the broken-for-this-binary
Ghidra path can give directly — a manual scan of call sites near the grid-based function
above is the next step).

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
   count. §4's per-reference-point nearest-neighbor scan is the right primitive for a
   real fix: it already computes, per point in the accumulated cloud, whether *any*
   recent frame matched it closely — the same computation, just inverted (flag reference
   points with **no** close match across recent frames, instead of only producing one
   aggregate ratio) directly yields a spatial "under-covered" point set, paintable back
   onto the live point cloud as a literal heatmap. That's a materially better answer than
   a single pass/fail gate with no spatial information, and it's the same KD-tree query
   already being run — no new expensive computation required, just keeping the per-point
   result instead of discarding it after the aggregate ratio is computed.
3. **Per-point/voxel confidence** (§3) — weighted TSDF-style fusion with a
   credibility-threshold extraction is a sound, standard approach already; the open
   question is calibration (`credible_ratio`, the per-point weight itself), not the
   architecture. Worth keeping the shape, revisiting the numbers.

Not yet done: tracing the actual per-point weight computation (§3's open question),
confirming the "Points" HUD counter = bundle-adjustment valid-pose-count hypothesis
(§2) against the Unity/IL2CPP side, decoding `compute_cloud_overlap_fast`'s grid-based
algorithm (§4, a separate ~3KB function from the now-fully-decoded point-based one),
and locating `e0a345aa`'s call site to confirm it's really the accept/reject gate
downstream of `b91078bd`'s scoring.
