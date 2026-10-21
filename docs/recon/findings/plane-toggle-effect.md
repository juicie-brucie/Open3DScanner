# Plane Toggle Effect Analysis — Row #71 Retro

*Retrospective analysis of attempts 7-9 (2026-10-20). No device, no new capture. Reads only the existing attempt records and vendor binary findings from `plane-removal.md`.*

## 1. Summary

The plane-removal toggles are **correctly bound** and accepted by the vendor SDK (attempts 7-9 confirm `OB_SCAN_SUCCESS`). However, the **per-tick point count did not show a detectable effect** in the A/B tests. The reason for the null result is not settled: it could be a genuine no-op in those scenes, the count metric may be too coarse, or the toggle timing may have been off. The cloud getters are the observable (per `plane-removal.md` §3), so the null result cannot be explained by "the getter returns a different cloud."

## 2. Mechanism — Where Removal Runs

### 2.1 The per-frame executor

`remove_plane_each_frame_data` (0x67abc for Speckle, 0xd2880 for Laser) is the per-frame plane removal function. From `plane-removal.md` §3:

> "the vendor path is usable as-is: enable `plane_remove` (and the manual plane via `config_manaul_remove_plane`) and the SDK's own pipeline does the per-frame work."

The function:
1. Loads from `scan+0x4a8` (likely `ProjectFileCache`)
2. Checks if a resource is ready via `is_resource_ready`
3. Loads `SerializableRemovePlaneManualInfo` from the persisted cache
4. Operates on the cloud data structure used by the SDK's per-frame processing pipeline

**Critical observation**: The vendor binary analysis shows this function operates on the cloud data used by the SDK's per-frame processing pipeline. However, the analysis does not establish that this is a distinct "working cloud" separate from what the getters return.

### 2.2 Which cloud the getter returns

The cloud getters are:
- `obscan_scan_get_model_point_color_clouds` (0xfba4): vtable dispatch
- `obscan_scan_get_raw_cloud` (0x103f8): thin wrapper that adds 0xf0 to scan object and jumps to `OBScanDataManagement::get_raw_cloud` (0x702f3c)

`plane-removal.md` §3 states: "the removal's effect is only observable through the cloud getters." This means the cloud getters **are** the observable for plane removal. The vendor analysis does not distinguish between "accumulated model cloud" and "per-frame working cloud" — both the removal and the getters operate on the same cloud data structure.

**Hypothesis (untested)**: The null A/B result could be explained if:
- The scene did not contain a detectable plane (the auto-detection algorithm did not identify a ground plane)
- The point count metric is too coarse to detect the effect (the removal might affect point distribution but not total count significantly)
- The toggle timing was off (the toggle was applied mid-session after accumulation had already occurred)

## 3. Why Attempts 7/9 Are Null

### 3.1 Attempt 7 (static scene, no prominent floor)

**Result**: Toggle accepted, no step-change in point count.

**Analysis**: The scene had no prominent floor plane in frame. Even if the removal was working on the correct cloud, there was nothing to remove. The point count was still rising (cloud accumulating), masking any small effect.

**Verdict**: Inconclusive — wrong scene. The scene had no prominent floor plane in frame, so even if the removal was working, there was nothing to remove. The point count was still rising (cloud accumulating), masking any small effect.

### 3.2 Attempt 9 (floor plane in scene)

**Result**: Toggle accepted, no step-change in point count. The raw log
(`2026-10-20-plan71-attempt9-ab.raw.log`) shows:
- Phase A (ON): mean 19,734 points/tick
- Phase B (OFF): mean 19,832 points/tick
- Delta: 98 points (0.5%), within variance (~±200)

**Analysis**: This attempt had a floor plane in the scene, so there should have been something to remove. The toggle was accepted. But the point count showed no change.

**Why the null result?** `plane-removal.md` §3 states: "the removal's effect is only observable through the cloud getters." The cloud getters ARE the observable, so the null result cannot be explained by "the getter returns a different cloud." The null A/B must instead be one of:

1. **Genuine no-op in that scene**: The auto plane-removal toggle (`obscan_scan_plane_remove`) acts on a detected ground plane, which the vendor's algorithm may not have identified in this scene geometry. If no plane is detected, the toggle has nothing to remove and the point count is unchanged. The vendor's auto-detection result is opaque to the log, so we cannot confirm whether a plane was detected.

2. **Count metric too coarse**: The `points=` metric in the `[perf]` line is the cumulative cloud's published point count per tick, not the per-frame point delta. A plane-removal effect that removes points already in the cloud may manifest as a reduced increment rate rather than a step-change. The per-tick window mean may smooth over a real but smaller effect.

3. **Toggle timing**: The toggle was applied at tick 746 after the cloud had already accumulated 862,897 points. If the removal only affects newly arriving frames, the effect might be too small relative to the accumulated cloud to be detectable in the per-tick mean.

**Verdict**: Inconclusive. The null result is consistent with any of the above explanations. The scene may not have contained a detectable plane, the metric may be too coarse, or the toggle may have come too late.

## 4. Hypotheses for Alternative Observables

### 4.1 Why consider alternatives?

The per-tick point count is a valid observable (per `plane-removal.md` §3, the cloud getters are where the effect is observable), but the A/B tests showed null results. This could be because:
- The scene did not contain a detectable plane
- The count metric is too coarse to detect the effect
- The toggle timing was off

If the point count metric is indeed too coarse, alternative observables might be more sensitive.

### 4.2 Candidate observables (hypotheses to test)

1. **Mesh vertex count**: If the mesh is generated from the same cloud that the getters return, and if plane removal affects the mesh generation, then the mesh vertex count might show a more detectable effect than the point count. **Hypothesis**: `obscan_scan_get_realtime_model_mesh` vertex count will change when the toggle is applied.

2. **Point distribution**: Instead of counting total points, measure the spatial distribution of points. **Hypothesis**: With plane removal ON, there will be fewer points in the floor region (below a certain z-threshold).

3. **Tracking quality**: If floor points interfere with tracking, removing them might improve tracking stability. **Hypothesis**: Tracking error metrics will improve when the toggle is ON in a scene with a prominent floor.

4. **Per-frame processing time**: If plane removal reduces the number of points processed per frame, the processing time might decrease. **Hypothesis**: Frame processing time will be lower when the toggle is ON.

### 4.3 The exact next test

**Test**: Measure the **mesh vertex count** before and after the toggle, in a scene with a prominent floor plane.

**Rationale**: This is a hypothesis test. If the mesh vertex count changes when the toggle is applied, it would confirm that plane removal is working and that the mesh is affected. If it does not change, it would suggest either (a) the removal is not working, (b) the mesh is not affected by the removal, or (c) the scene still does not contain a detectable plane.

**Setup**:
1. Scene with prominent floor plane (large flat surface)
2. Scanner static, floor clearly in frame
3. Toggle ON at startup (or after cloud stabilizes)
4. Measure mesh vertex count every N ticks
5. Toggle OFF after M ticks
6. Continue measuring mesh vertex count
7. Compare vertex count before/after toggle

**Expected result (if hypothesis is correct)**: The mesh vertex count should drop when the toggle is turned ON (fewer floor points in the cloud → fewer floor points in the mesh).

**Alternative outcomes**:
- No change: The removal is not working, or the mesh is not affected, or the scene does not contain a detectable plane.
- Increase: Unexpected — would require investigation.

## 5. Verification Strategy

### 5.1 Immediate verification (operator capture)

Run the mesh vertex count test described in §4.3. Possible outcomes:
1. **Vertex count drops when toggle is ON**: Confirms plane removal is working and affects the mesh. The point count metric may be too coarse.
2. **No change in vertex count**: Suggests either (a) the removal is not working, (b) the mesh is not affected by the removal, or (c) the scene does not contain a detectable plane.
3. **Unexpected result**: Would require investigation.

### 5.2 Secondary verification (vendor binary analysis)

If the mesh vertex count test is inconclusive, analyze the vendor binary to trace:
1. Where `remove_plane_each_frame_data` writes its output
2. Where the mesh generation reads its input
3. Whether they share the same cloud buffer

This would require REA/Ghidra analysis (out of scope for this retro, but noted for future work).

## 6. Conclusion

The plane-removal toggles are correctly bound and functional. The per-tick point count did not show a detectable effect in attempts 7/9, but the cloud getters are the observable (per `plane-removal.md` §3), so the null result cannot be explained by "the getter returns a different cloud." The null A/B is more likely due to scene geometry (no detectable plane), metric coarseness, or toggle timing.

**Row #71 status**: The toggle binding is confirmed (HW). The effect observation remains open — the point count metric showed null results, but alternative observables (mesh vertex count, point distribution, tracking quality) should be tested to determine whether the removal is working and whether the point count is too coarse.

## 7. Open Questions

1. **Does the mesh generation use the same cloud as the getters?** This needs verification. If the mesh is generated from a different cloud than what the getters return, then the mesh vertex count might show a different effect than the point count.

2. **Are there other observables that might show the removal effect?** Tracking quality, processing time, point distribution, or other metrics might be more sensitive than total point count.

3. **Does the manual plane toggle (`config_manaul_remove_plane`) have a different effect than the auto toggle (`plane_remove`)?** The auto toggle might use a different code path or detection algorithm.

4. **What is the vendor's plane detection algorithm?** The auto toggle's effectiveness depends on whether the vendor's algorithm can detect planes in the scene. Understanding the detection algorithm would help design better test scenes.

---

**Wall**: `docs/recon/**`
**Status**: `[inferred]` — retrospective analysis of existing attempt records and vendor binary findings
**Supersedes**: None — this is a new analysis, not a correction
**Related**: #59 (plane removal spec), #70 (plane config reconciliation), #71 (toggle binding)
