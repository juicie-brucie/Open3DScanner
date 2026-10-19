# The vendor's quality gate and the quality signals (row #75)

*Static half, plan75 pass. The frozen corpus
(`scan-quality-and-coverage-reverse-engineering.md` §2) was used as a pointer;
every claim below is re-derived and address-cited.*

## 1. What the gate computes — the valid-frame count (verified), gated at 60

`laser::OBscanLaserCommon::get_opt_valid_frame_num(OBScanModeConfig const&
config, LaserScanningContextInterface*, int& out_count) -> ObScanStatus`
(`0x9ac70`, verified instruction by instruction):

- `out_count = 0`; then the branch is on **`config.fuse_mode`** (`config+0x14`):
  `== 3` (`OB_SCAN_FUSE_MODE_MARKERLESS`) →
  `LaserTextureScanning::get_optimization_recored_valid_frames_result()`
  (`0x97573c`) → `out_count = the vector's size`;
- every other mode (including `MARKER = 2` — what our tooling uses) →
  `MKMarkerScanningContext::get_optimization_results()` (`0xc2b2d0`) →
  `mkscan::MarkerGlobalOptResult::cref__xform_tf_valid_poses()` (`0xc21e4c`) →
  `out_count = the valid poses' count` — the **bundle adjustment's trusted
  poses** (the guarding string "the ba result is nullptr" at `0x9ad20` ✓).

So the corpus's mechanism survives re-derivation: for marker-tracked scanning,
"enough data" is counted in **valid BA poses**, not points. Two refinements the
corpus did not carry:

- **The threshold is concrete and lives in the SDK:**
  `OrbbecScanLaser::global_optimization` (`0xd1440`: `cmp w0, #0x3b; b.gt`)
  logs **"too few frames (%d) for global opt"** and bails unless the count is
  **> 59** — the SDK's global optimization needs **at least 60 counted
  frames**. The marker-optimization sibling
  (`laser::OBScanLaserMarkerOptimization::global_optimization`, same message at
  `0xa50b8`) carries the same gate.
- **The count is also a fusion input**, not only a gate: it is consumed by
  `OBScanLaserFusionFast::curves_fit` (`0xb7400`) and three
  `OBScanLaserFusionNormal` paths (`0xbc594`/`0xbc948`/`0xbee9c`).

**The counts are not C-reachable** — neither `get_opt_valid_frame_num` nor
`OBScanDataManagement::get_scanned_frame_num` (`0x703744`) has a C export. What
*is* reachable is the SDK's own failure signal:
`OrbbecScanSpeckle/Laser::realtime_scan_func` (`0x826b8`/`0xd9b18`) emits
process message **`OB_SCAN_MESSAGE_ID_SCANNING_FAILED`** (341967107,
`0x14620103`), and `incremental_fusion_change_voxelsize_request` (`0x700f84`)
emits `OPTIMIZATION_TO_MESH_NEED_CHANGE_VOXEL_SIZE` (341967108) — both ride the
process listener we already bind.

## 2. A per-point/per-region signal? No — but a per-frame tracking-status enum, exported

There is **no per-point or per-region coverage/quality signal** in the export
surface (checked against `macos-exports.txt`; the only quality-ish names are
`obscan_calib_get_calibration_score` — calibration — and
`obscan_device_management_is_marker_less_param_valid` — a config check).

There **is** a per-frame (per-pose) one, and it is already declared: the
frame's tracking status, `obscan_frame_get_frame_tracking_status(frame,
int32_t*)`. Its value set is recovered from the SDK's own name/description
table (plaintext, `0x42eb0xx`):

**The numeric mapping is pinned (row #78)** — read from the vendor's own
lookup functions `_OBScanTrackingStatus_name` (`0x170d64`) and `_desc`
(`0x170d88`): each bounds-checks `index <= 6` and loads from a 7-entry pointer
table (`0x45455b0` names / `0x45455e8` descs); out of range returns the shared
`"UNKNOWN"` (`0x42dfae9`).

| value | name | description |
| --- | --- | --- |
| `0` | `OB_SCAN_TRACKING_SUCCESS` | "tracking success" |
| `1` | `OB_SCAN_TRACKING_LOST` | "tracking lost" |
| `2` | `OB_SCAN_TRACKING_AMBIGUOUS_PLANE` | "ambiguous plane" |
| `3` | `OB_SCAN_TRACKING_FAILURE_INSUFF_VALID_POINTS` | "insufficient valid points" |
| `4` | `OB_SCAN_TRACKING_FAILURE_TEXTURELESS` | "textureless" |
| `5` | `OB_SCAN_TRACKING_FAILURE_MARKERLESS` | "markerless failure" |
| `6` | `OB_SCAN_TRACKING_FAILURE_MOVING_TOO_FAST` | "moving too fast" |

Note what this means for the corpus's argument: the string it leans on
("insufficient valid points") is **this enum's value 3's description** — a
per-frame *diagnostic*, not a point count. The enum is declared in the header
(`ObScanTrackingStatus`) so a consumer can render the vendor's own reason from
the raw getter value. Cross-check: `send_frame_msg`'s
`set_frame_tracking_status_unsafe` calls (`0x5f458`/`0x5f4f0`, `0xc6f18`/
`0xc7048`) write `5` (`OB_SCAN_TRACKING_FAILURE_MARKERLESS`) and a computed
value — consistent with the mapping, though the state machine that chooses
between them is not part of this row.

## 3. What our coverage layer can align with

- **Per-frame quality/reason: yes.** The tracking status above hands G3 a
  per-pose reason taxonomy straight from the vendor ("textureless", "moving too
  fast", "insufficient valid points", "ambiguous plane"), delivered per
  `FRAME_READY` payload (one-shot, like the rest of the frame family — read it
  with the other frame getters in the same pass).
- **Per-point/per-region coverage: ours to compute.** The vendor exposes the
  live cloud (`obscan_scan_get_model_point_color_clouds`, `…_get_raw_cloud`)
  and the poses (`obscan_scan_get_registration_pose`, `…_get_global_pose`) —
  and nothing else. The heatmap is our math over those.
- **The gate's count: cannot align, only approximate.** The valid-pose count
  and its 60-frame threshold are internal; we can reproduce the *inputs* (our
  own frame count from `FRAME_READY`, the per-frame tracking status) but not
  the bundle adjuster's *validity decision* (which frames it trusted). Any
  "is there enough data" heuristic we build must be ours, with the vendor's
  enum as the per-frame cross-check.

**Residual (listing-follow-up):** the `send_frame_msg` state machine that
*chooses* a status (`0x5f44c`–`0x5f500`, `0xc6f0c`–`0xc7058`) — which conditions
produce which value. The mapping itself is pinned above.
