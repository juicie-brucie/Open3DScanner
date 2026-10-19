# The tracking mechanism — status, marker BA, and the owned-tracker seam (row #80)

*Static half, plan80 pass (row #80); §1's fresh-value origin settled by row #82.
Re-derived for G3 (the corpus was used as a pointer only); addresses and shapes,
no decompiled output.*

## 1. Where the per-frame tracking status is set

**The field.** The frame's tracking status lives on the frame impl at `+0x98`:
`OBScanFrame_::get/set_frame_tracking_status_unsafe` (`0x6b2ea4`/`0x6b2eb8`)
are `ldr x8,[x0]; ldr/str w?,[x8,#0x98]` — the C export
`obscan_frame_get_frame_tracking_status` reads exactly that word. Values are the
enum pinned in row #78 (0 SUCCESS … 6 FAILURE_MOVING_TOO_FAST).

**The fresh value (settled, row #82).** The frame impl is
`OBScanFrameInterface`: `OBScanFrame_::C2Ev` (`0x6b1f1c`, reached through the
`C1Ev` thunk at `0x6b2150`) allocates a 0x2e0-byte impl and constructs it at
`0x6b1f84`. Two writers produce a fresh value:
1. the impl's constructor sets `+0x98 = 1` (`OB_SCAN_TRACKING_LOST`) at
   `0x6b1bc4` — the fail-safe default for an untouched frame; and
2. the per-frame producer overwrites it: each scanning context's
   `generate_show_datas(bool,bool)` copies its own status member (`context+0x18`)
   into the fresh frame's `impl+0x98` — `SpeckleScanningContextInterface`
   (`0x898a80`–`0x898a84`), `LaserScanningContextInterface`
   (`0x95ce20`–`0x95ce24`), `MarkerFrameWorkScanningContext`
   (`0xc1a740`–`0xc1a744`).

The context's `+0x18` is the live per-frame verdict, written by the processing
steps (values observed): Speckle `read_depth_point_cloud(bool,bool)` → `3`
(`0x8974a4`), `5` on a ≤3-element guard (`0x897c44`), `3` (`0x897f48`), `2`
after an `is_plane_depth_image` hit (`0x898578`); `cached_detect_3d_markers` →
`5` on a marker-count guard (`0x89f270`); `judge_depth_satisfy_following(bool,
Frame*)` → `3` (`0x89f6dc`); Laser `scanning_following` → `1` (`0x953878`);
`scanning_relocation` → `1` (`0x955088`), `0`/`1` (`0x955278`);
`read_depth_point_cloud` → `3` (`0x95b878`), `5` (`0x95c508`); Marker
`scanning_following` → `1` (`0xc17af0`); `scanning_relocation` → `1`
(`0xc19418`), `1` (`0xc195c4`); `read_depth_point_cloud` → `3` (`0xc19898`),
`5` (`0xc1a5bc`). Frames are therefore born `LOST`, and every status a consumer
sees is one of these verdicts or the publish path's debounced derivative of one.

**The publish path.** The setter's only callers are
`OrbbecScanSpeckle/Laser::send_frame_msg` (`0x5f458`/`0x5f4f0`,
`0xc6f18`/`0xc7048`). The Speckle body (`0x5f3e4`–`0x5f5a0`) first copies the
caller's frame (`OBScanFrame_::C1ERKS_` at `0x5f414`; the copy **shares the
impl** — refcount bump at `0x6b22b0` — so its read sees the producer's value),
reads the status (`0x5f44c`), sets the copy's status to the scan's carried value
(`scan+0xc80`, `0x5f458`), then — only when ≥500 has elapsed on the `StdTimer`
at `scan+0xc70` (`0x5f45c`–`0x5f4b0`; `StdTimer::duration` at `0x1680bc`
returns ticks×1e-6, i.e. ms for a ns tick source `[inferred]`) — recomputes it
with a **hysteresis**: if the producer's value is `5` (`FAILURE_MARKERLESS`), a
counter (`scan+0xc8c`) increments and the first **two** update windows report
**`1` (`OB_SCAN_TRACKING_LOST`)**, the third onward `5`; any non-5 resets the
counter. The result is stored back to `scan+0xc80` and set on the copy
(`0x5f4e8`/`0x5f4f0`); between update windows the published status is the
carried value. IR-marker devices additionally drive the scan-indicator LED from
the effective status (`0x5f4f4`–`0x5f554`,
`OrbbecDevice::set_scan_indicator_trace_lost_state`).

**The row #80 named reads were misdirected.** `RingBufferFrameMsgImpl` is the
ring-buffer message impl, not the frame impl (its `push` at `0x6b0980` is a
4-byte tail-branch into the shared `msg_buffer_push_impl` template); and
`realtime_scan_func` constructs no frames — the `OBScanFrame_` constructor call
sites are exhaustive: the three `generate_show_datas` paths above plus four
texture `showData` sites (`0x87e564`, `0x87e6b4`, `0x96e3a4`, `0x96e570`).

## 2. What the marker-based bundle adjustment consumes and produces

**The solver is `mkscan::BaSolverContext` (Ceres).** `solve(MkPublicOptions
const&, ProgressReporter const&)` (`0x933294`) is called from the three global
optimization paths — `OrbbecScanSpeckle::marker_global_optimization`
(`0x6e2f4`), `laser::OBScanLaserMarkerOptimization::global_optimization`
(`0xa51b8`), `OrbbecScanLaser::global_optimization` (`0xd1558`). Each cite is
**the `solve()` call site** inside that function (~0x100 past
`scan-quality-gate.md` §1's cites of the same functions, which are the "too few
frames" gate instructions) — the same paths that read
`BaSolverContext::get_ba_info()` (2–3 reads each) and enforce row #75's
`>= 60` valid-frame gate. `_opt_reassociate_markers` (`0x9301dc`) is
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

**Settled (row #82):** the status's fresh-value origin — the impl constructor's
fail-safe `1` plus the producer override in `generate_show_datas` (§1).

**Not statically settleable:** the BA's internal validity criterion (which
poses it trusts — a runtime-numeric decision inside the Ceres solve; named, not
derivable) and `MkPublicOptions`' contents (runtime configuration).
