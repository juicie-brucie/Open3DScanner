# The tracking mechanism — status, marker BA, and the owned-tracker seam (row #80)

*Static half, plan80 pass. Re-derived for G3 (the corpus was used as a pointer
only); addresses and shapes, no decompiled output.*

## 1. Where the per-frame tracking status is set

**The field.** The frame's tracking status lives on the frame impl at `+0x98`:
`OBScanFrame_::get/set_frame_tracking_status_unsafe` (`0x6b2ea4`/`0x6b2eb8`)
are `ldr x8,[x0]; ldr/str w?,[x8,#0x98]` — the C export
`obscan_frame_get_frame_tracking_status` reads exactly that word. Values are the
enum pinned in row #78 (0 SUCCESS … 6 FAILURE_MOVING_TOO_FAST).

**The only writers** are `OrbbecScanSpeckle/Laser::send_frame_msg`
(`0x5f458`/`0x5f4f0`, `0xc6f18`/`0xc7048`) — the frame-publish path. The
Speckle body (`0x5f44c`–`0x5f4f4`) reads the (shared, carried) status, sets the
new frame's status to the scan's stored one (`scan+0xc80`), then applies a
**hysteresis**: if the carried status is `5` (`FAILURE_MARKERLESS`), a counter
(`scan+0xc8c`) increments and the first **two** consecutive occurrences are
reported as **`1` (`OB_SCAN_TRACKING_LOST`)**, the third onward as `5`; any
non-5 resets the counter. The result is written back to `scan+0xc80` and set on
the frame. So the HUD's reason for a markerless failure is deliberately
debounced — worth mirroring if we render it.

**The fresh-value origin is the open part.** Every writer of the scan's
`+0xc80` and the impl's `+0x98` is this same path (checked by offset sweep), so
the status is a **carried/shared state** (the published frames' impl is shared
with the scan's frame template). No static path in the swept set writes a fresh
tracking value; it can only enter through the frame impl's construction or the
tracking code that builds frame payloads. **Sharper follow-up:** read
`RingBufferFrameMsgImpl`'s constructor and the realtime scan loop's frame
building (`OrbbecScanSpeckle/Laser::realtime_scan_func`, `0x826b8`/`0xd9b18` —
the SCANNING_FAILED emitters) for a `+0x98` initializer/other accessor.

## 2. What the marker-based bundle adjustment consumes and produces

**The solver is `mkscan::BaSolverContext` (Ceres).** `solve(MkPublicOptions
const&, ProgressReporter const&)` (`0x933294`) is called from the three global
optimization paths — `OrbbecScanSpeckle::marker_global_optimization`
(`0x6e2f4`), `laser::OBScanLaserMarkerOptimization::global_optimization`
(`0xa51b8`), `OrbbecScanLaser::global_optimization` (`0xd1558`) — the same
paths that read `BaSolverContext::get_ba_info()` (2–3 reads each) and enforce
row #75's `>= 60` valid-frame gate. `_opt_reassociate_markers` (`0x9301dc`) is
the outlier/re-association step.

**Consumes** — the Ceres residuals (all `mkscan::`):
- `solve_stereo_reproj_and_ref_rod_rescale` (`0x93f948`) and
  `…_reparameterize` (`0x943aa8`) — **stereo reprojections** (the marker
  observations) plus the **reference rods** (the `obscan_session_reference_rod_*`
  exports from row #60's family);
- `solve_ref_rod_match_distance` (`0x93f1f4`) — the rod-distance term;
- the observations come from the marker matching —
  `mkscan::nn_match_with_check` (`0xe17f8`) and `mkscan::mf_tri_match`
  (`0xe2f5c`) — via the `lib_match_speckle2v2::MarkerGraph`
  (`free_scan::convert_marker_data`, `0x8a6844`).

**Produces** — `get_ba_info()` and the per-frame results:
`MKMarkerScanningContext::get_optimization_results()` (`0xc2b2d0`) returns a
vector of per-frame shared pointers (16-byte elements); `fusion_check_ba`
(`0xa5400`) walks them and reads each frame's
`MarkerGlobalOptResult::cref__xform_tf_valid_poses()` (`0xc21e4c`) — the
per-frame **valid poses** (128-byte transforms = 4×4 doubles, the same shape as
our `ObScanPose`), which is exactly the count row #75's gate uses. The Ceres
signatures also carry `vector<array<double,4>>` + `vector<array<double,3>>`
(quaternion + translation arrays) — the solved pose parameters. The
registration side (row #40) writes the scan's registration pose via
`OBScanRegistration::auto_regist_func`/`point_pair_regist_func`, which the pose
getters read back.

## 3. The owned-tracker seam

**Consume (already declared, no vendor internals needed):** the per-frame
tracking status (`obscan_frame_get_frame_tracking_status`, with the hysteresis
noted above), the frame pose, the markers (`obscan_scan_get_markers`), the
registration/global poses, and the live cloud — i.e. everything G2/G3's
feedback layer already binds.

**Reimplement (if the vendor tracker is replaced):** the marker matching
(`mf_tri_match`/`nn_match_with_check`), the BA (the stereo-reprojection +
reference-rod Ceres problem → per-frame poses + validity), and the
validity/status logic (the trusted-pose decision plus the debounce). The
interfaces to align with are ours already: the pose convention
(`scan-live-data.md` §17.1), the marker record semantics (§17.3), and the
tracking-status enum (row #78) — so an owned tracker can feed the same getters'
data model without new ABI.

**Cannot be settled statically:** the BA's internal validity criterion (which
poses it trusts — inside the Ceres solve; named, not derivable), the status's
fresh-value origin (§1's follow-up), and `MkPublicOptions`' contents (the BA's
configuration).
