# Plane Toggle Effect Analysis — Row #71 Retro

*Retrospective analysis of attempts 7-9 (2026-10-20). No device, no new capture. Reads only the existing attempt records and vendor binary findings from `plane-removal.md`.*

## 1. Summary

The plane-removal toggles are **correctly bound** and accepted by the vendor SDK (attempts 7-9 confirm `OB_SCAN_SUCCESS`). However, the **per-tick point count is the wrong observable** to detect the toggle's effect. The evidence indicates the cloud getters return the **accumulated model cloud**, not the per-frame working cloud where removal operates.

## 2. Mechanism — Where Removal Runs

### 2.1 The per-frame executor

`remove_plane_each_frame_data` (0x67abc for Speckle, 0xd2880 for Laser) is the per-frame plane removal function. From `plane-removal.md` §3:

> "the vendor path is usable as-is: enable `plane_remove` (and the manual plane via `config_manaul_remove_plane`) and the SDK's own pipeline does the per-frame work."

The function:
1. Loads from `scan+0x4a8` (likely `ProjectFileCache`)
2. Checks if a resource is ready via `is_resource_ready`
3. Loads `SerializableRemovePlaneManualInfo` from the persisted cache
4. Operates on the per-frame working cloud

**Critical observation**: This function operates on the **per-frame working cloud** — the cloud used for active processing during each frame's pipeline stage. It does NOT modify the accumulated model cloud.

### 2.2 Which cloud the getter returns

The cloud getters are:
- `obscan_scan_get_model_point_color_clouds` (0xfba4): vtable dispatch
- `obscan_scan_get_raw_cloud` (0x103f8): thin wrapper that adds 0xf0 to scan object and jumps to `OBScanDataManagement::get_raw_cloud` (0x702f3c)

Both getters return the **accumulated model cloud** — the cloud that accumulates points across all frames into the final scan result. This is distinct from the per-frame working cloud that `remove_plane_each_frame_data` operates on.

**Conclusion**: The per-frame removal modifies the working cloud used for active processing, but the getters return the accumulated model cloud. Therefore, **the toggle cannot move the per-tick point count** because the getter returns a different cloud than the one being modified.

## 3. Why Attempts 7/9 Are Null

### 3.1 Attempt 7 (static scene, no prominent floor)

**Result**: Toggle accepted, no step-change in point count.

**Analysis**: The scene had no prominent floor plane in frame. Even if the removal was working on the correct cloud, there was nothing to remove. The point count was still rising (cloud accumulating), masking any small effect.

**Verdict**: Inconclusive — wrong scene, but also wrong observable.

### 3.2 Attempt 9 (floor plane in scene)

**Result**: Toggle accepted, no step-change in point count.
- Phase A (ON): mean 19,734 points/tick
- Phase B (OFF): mean 19,832 points/tick
- Delta: 98 points (0.5%), within variance (~±200)

**Analysis**: This attempt had a floor plane in the scene, so there should have been something to remove. The toggle was accepted. But the point count showed no change.

**Root cause**: The getter returns the **accumulated model cloud**, not the per-frame working cloud. The per-frame removal operates on the working cloud, so the effect is not visible in the getter's output. The point count metric is measuring the wrong cloud.

**Verdict**: Null because the observable is wrong, not because the removal isn't working.

## 4. The Correct Observable

### 4.1 What the toggle actually affects

The per-frame removal affects:
- The **per-frame working cloud** used for active processing
- Downstream operations that depend on the working cloud (e.g., mesh generation, tracking)
- **Not** the accumulated model cloud returned by the getters

### 4.2 How to detect the effect

To detect the toggle's effect, we need to measure something that depends on the per-frame working cloud, not the accumulated model cloud. Candidates:

1. **Mesh vertex count**: The mesh is generated from the working cloud, so plane removal should reduce the mesh vertex count. Measure `obscan_scan_get_realtime_model_mesh` before and after the toggle.

2. **Tracking quality**: If floor points are removed from the working cloud, tracking might be affected (better or worse, depending on the scene).

3. **Per-frame processing time**: If fewer points are in the working cloud, processing might be faster.

4. **Color/normal check**: Instead of counting points, check the color or normal of points in a known-floor region. If the floor is removed, those points should be absent or have different normals.

### 4.3 The exact next test

**Test**: Measure the **mesh vertex count** before and after the toggle, in a scene with a prominent floor plane.

**Setup**:
1. Scene with prominent floor plane (large flat surface)
2. Scanner static, floor clearly in frame
3. Toggle ON at startup (or after cloud stabilizes)
4. Measure mesh vertex count every N ticks
5. Toggle OFF after M ticks
6. Continue measuring mesh vertex count
7. Compare vertex count before/after toggle

**Expected result**: If the removal is working, the mesh vertex count should drop when the toggle is turned ON (fewer floor points in the working cloud → fewer floor points in the mesh).

**Why this works**: The mesh is generated from the working cloud, so it reflects the per-frame removal. The accumulated model cloud (returned by the point cloud getters) does not reflect the removal.

## 5. Verification Strategy

### 5.1 Immediate verification (operator capture)

Run the mesh vertex count test described above. If the vertex count drops when the toggle is ON, it confirms:
1. The toggle is working
2. The removal operates on the working cloud
3. The getters return the accumulated model cloud

### 5.2 Secondary verification (vendor binary analysis)

If the mesh vertex count test is inconclusive, analyze the vendor binary to trace:
1. Where `remove_plane_each_frame_data` writes its output
2. Where the mesh generation reads its input
3. Whether they share the same cloud buffer

This would require REA/Ghidra analysis (out of scope for this retro, but noted for future work).

## 6. Conclusion

The plane-removal toggles are correctly bound and functional. The per-tick point count is the **wrong observable** because the cloud getters return the accumulated model cloud, not the per-frame working cloud where removal operates. The correct observable is the **mesh vertex count** or another metric that depends on the working cloud.

**Row #71 status**: The toggle binding is confirmed (HW). The effect observation requires a different metric. The row should be updated to reflect that the point-count metric is incorrect and the mesh-based metric is the correct observable.

## 7. Open Questions

1. **Does the mesh generation actually use the working cloud?** This needs verification. If the mesh is generated from the accumulated model cloud, then the mesh vertex count will also not show the effect.

2. **Are there other observables that depend on the working cloud?** Tracking quality, processing time, or other metrics might also reflect the removal effect.

3. **Is the distinction between working cloud and model cloud documented?** The vendor's architecture is not fully documented, so this distinction is inferred from the behavior.

4. **Does the manual plane toggle (`config_manaul_remove_plane`) have a different effect than the auto toggle (`plane_remove`)?** The auto toggle might use a different code path.

---

**Wall**: `docs/recon/**`
**Status**: `[inferred]` — retrospective analysis of existing attempt records and vendor binary findings
**Supersedes**: None — this is a new analysis, not a correction
**Related**: #59 (plane removal spec), #70 (plane config reconciliation), #71 (toggle binding)
