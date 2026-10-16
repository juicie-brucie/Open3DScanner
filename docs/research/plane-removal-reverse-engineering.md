# CrealityScan's live plane removal: reverse-engineered ABI and algorithm

Research notes on how CrealityScan's "Exclude Flat Base" feature actually works,
reverse-engineered from `liborbbec_scan.dylib` (arm64 slice, manual `nm`/`otool -tV`
disassembly — Ghidra's full auto-analysis of this ~45MB binary consistently exceeds
this environment's tool timeout, so this was done by hand rather than decompiled).
Motivated by the user's own experience: "The dynamic flat plane removal during
scanning in Creality Scan is garbage. It does not work at all" — current workflow is
leaving it off and cutting the base away in post. This doc is groundwork for OpenScanner
doing **live** plane removal properly, which matters for resource management (less data
to carry/process per frame during an active scan), not just a post-process cosmetic cut.

## 1. The exported C ABI (same tier as the rest of `obscan_api.h`)

Found via `nm -arch arm64 -gU liborbbec_scan.dylib`, same naming convention as every
other `obscan_scan_*` binding this project already uses:

```
_obscan_scan_plane_remove
_obscan_scan_turntable_plane_remove
_obscan_scan_config_manaul_remove_plane   (vendor's own typo — "manaul", not "manual")
_obscan_scan_get_manaul_remove_plane
```

All four are thin trampolines: they log their call (`obscan_scan_plane_remove(%s)` with
true/false), null-check the scan handle, then dispatch through the handle's vtable
(`br` to a function pointer loaded from `*handle + offset`) or, for the getter, call a
concrete C++ method directly. Disassembled signatures (ARM64 AAPCS register usage):

```c
// vtable slot +0x50 — toggles OBScanDataManagement::update_realtime_auto_rm_plane
typedef ObScanStatus (*fn_obscan_scan_plane_remove)(ObScanScanHandle scan, bool enable);

// vtable slot +0x60 — toggles OBScanDataManagement::update_realtime_rm_turntable_plane
typedef ObScanStatus (*fn_obscan_scan_turntable_plane_remove)(ObScanScanHandle scan, bool enable);

// vtable slot +0x58 — toggles OBScanDataManagement::update_realtime_manual_rm_plane
typedef ObScanStatus (*fn_obscan_scan_config_manaul_remove_plane)(ObScanScanHandle scan,
                                                                   const OBScanManualRPConfig_* config);

// Calls OBScanBase::get_manaul_remove_plane(OBScanManualRPConfig_, OBScanPlane_*) directly (not virtual)
typedef ObScanStatus (*fn_obscan_scan_get_manaul_remove_plane)(ObScanScanHandle scan,
                                                                const OBScanManualRPConfig_* in_config,
                                                                OBScanPlane_* out_plane);
```

The three toggle/config functions land on **sequential vtable slots** (0x50, 0x58,
0x60 — 8 bytes apart), which lines up exactly with `OBScanDataManagement`'s three
`update_realtime_*_rm_plane` methods (found independently via `nm`) being adjacent
virtual overrides declared in that order: auto, manual, turntable. Not dynamically
confirmed (would need a live debugger attached to the vtable), but the address pattern
and 1:1 argument-shape match (bool / bool+plane+int / bool) make this a high-confidence
read.

### Structs, recovered from the disassembly's exact byte offsets

```c
// 16 bytes — plane equation coefficients, standard form.
typedef struct OBScanPlane_ {
    float a, b, c, d;  // point p is "on the plane" when a*p.x + b*p.y + c*p.z + d == 0
} OBScanPlane_;

// 24 bytes (0x18) — confirmed by the getter copying exactly this many bytes.
typedef struct OBScanManualRPConfig_ {
    int32_t unknown_0;  // 0x00 — passed as "int&" to the underlying virtual call; purpose
                         //         not yet determined (frame index? point-cloud slot?)
    bool enable;         // 0x04
    uint8_t pad[3];       // 0x05
    OBScanPlane_ plane;   // 0x08
} OBScanManualRPConfig_;
```

## 2. Three distinct modes, not one

The UI only ever showed a single "Exclude Flat Base" checkbox in every session this
project has captured (`scanParamConfig.dat`'s `"PlaneRemoveProperty"` is one bool). But
the native SDK actually implements three independent concepts:

- **Auto** (`obscan_scan_plane_remove`) — detects and removes a flat base plane on the
  fly from the accumulating point cloud itself, no setup required. This is almost
  certainly what "Exclude Flat Base" maps to, and so almost certainly what's "garbage."
- **Turntable** (`obscan_scan_turntable_plane_remove`) — assumes a literal turntable
  accessory with a known/calibrated rotation axis and base height; likely unused unless
  that accessory is configured, and not exposed in the Scan Settings panel this project
  has seen.
- **Manual** (`obscan_scan_config_manaul_remove_plane`) — the caller supplies the plane
  equation directly (`OBScanManualRPConfig_`), rather than relying on auto-detection.
  `RemovePlaneManualInfo`/`SerializableRemovePlaneManualInfo` (seen in `nm` output,
  persisted via `OBScanSerialIO::load_remove_plane_manual_info`) suggest this is what
  backs whatever manual trim/cut tool exists elsewhere in the app, separate from the
  live scan settings.

## 3. The per-point decision function — likely root cause of "doesn't work"

`laser::OBscanLaserCommon::judge_rm_plane_is_need_cut(const v3d& point, const
OBScanPlane_& plane, const double& threshold) -> bool`, fully decoded from its
disassembly (pure scalar FP math, no branches into other functions):

```c
bool judge_rm_plane_is_need_cut(v3d point, OBScanPlane_ plane, double threshold) {
    // Standard signed point-to-plane distance along the plane's normal.
    double dist = (plane.a * point.x + plane.b * point.y + plane.c * point.z + plane.d)
                  / sqrt(plane.a*plane.a + plane.b*plane.b + plane.c*plane.c);

    if (dist < threshold) {
        return true;  // close to (or on the far side of) the plane -> cut
    }

    // Far from the plane along its normal: fall back to a literal Z-axis
    // ray/plane intersection instead of the normal-distance test.
    double plane_z_at_xy = -(plane.a * point.x + plane.b * point.y + plane.d) / plane.c;
    if (point.z <= plane_z_at_xy) {
        return false;  // point sits at/below the plane's surface here -> keep
    }
    return true;  // point sits above the plane's surface here -> cut
}
```

This is a genuinely odd hybrid: the primary test is a clean, correct plane-distance
threshold, but once a point clears that threshold the function *doesn't just keep it*
(the behavior you'd want for real object geometry standing clear of the base) — it
falls through to a second, Z-axis-specific test that can still mark it for cutting.
Whether "above the plane in Z" means "part of the object, away from the camera" or
"part of the object, standing up" depends entirely on which coordinate convention is in
effect at this call site (camera space vs. a calibrated world space), and a plane that's
even slightly mis-fit (see below) will make this secondary branch fire constantly,
misclassifying real object geometry near the base/edges — exactly the kind of behavior
that would look like "doesn't work at all" to a user watching chunks of their object
disappear unpredictably.

Where the plane itself comes from: `rs_common::plane_fit(BufferView<const v3f>, v3f&
normal_out, v3f& point_out)` and `OBPointCloud::plane_fit(...)` — the auto-detect path
presumably runs this over some candidate subset of points each frame/interval to keep
the tracked plane current, and any instability or poor-fit tendency there would also
explain inconsistent results independent of the `judge_rm_plane_is_need_cut` branch
above.

## 4. The actual bulk removal — simple and reimplementable

`free_scan::point_cloud_remove_plane_below(const OBPointCloud& in, OBPointCloud& out,
float a, float b, float c, float d)` — confirmed via disassembly: validates the four
plane-equation floats aren't NaN/Inf, resizes `out` based on `in`'s point count (16
bytes/point, matching `ObScanPoint {x,y,z,intensity}`), then filters. This is a plain
vectorized half-space point filter — no magic, straightforward to reimplement
independent of any of CrealityScan's own (possibly buggy) decision logic. A
`point_cloud_remove_plane_of_equation` sibling exists too (keeps points *at* the plane
rather than below it, by the name).

## 5. Implications for OpenScanner's own live plane removal

Two paths, not mutually exclusive:

1. **Drive the vendor's own calls.** Bind the four ABI functions above into
   `obscan_api.h`/`obscan_loader.h` (identical pattern to
   `obscan_scan_config_combined_exposure_and_laser_current`) and A/B test
   `obscan_scan_plane_remove` against what the real app does — confirms or refutes the
   hybrid-logic theory above against real hardware, cheaply, before writing any new
   geometry code.
2. **Skip the vendor's logic entirely.** For *live* resource-management purposes (the
   user's actual goal — cut data volume during capture, not just before export), a
   clean reimplementation is simple and avoids inheriting whatever is actually wrong
   with `judge_rm_plane_is_need_cut`: fit a plane once (or periodically) via a standard
   RANSAC/least-squares fit over the accumulating `obscan_scan_get_raw_cloud`/frame
   data, then apply a plain signed-distance threshold (no Z-axis fallback branch) each
   frame before the point ever gets handed to the UI/accumulation step. This is also
   where the project's existing `mesh/include/openscanner/mesh_tools.h` (Manifold
   boolean ops) could do the *final*, post-scan cut precisely via half-space CSG
   subtraction — distinct from the *live*, per-frame filter this doc is mainly about,
   and already scoped separately as a later task.

Not yet done: confirming any of this against real hardware (would need the four
functions actually bound and called, which hasn't happened yet), and tracing exactly
where in the per-frame pipeline `OrbbecScanLaser::frame_remove_plane(bool)` /
`frame_remove_turntable_plane(bool)` get invoked relative to the rest of frame
processing (found via `nm`, not yet disassembled).
