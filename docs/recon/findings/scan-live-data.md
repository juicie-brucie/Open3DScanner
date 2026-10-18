# Scan family — `obscan_scan_get_raw_cloud`: the show cloud and its publish gate (PLAN #23)

**Target:** `liborbbec_scan.3.5.3.dylib`, arm64 slice
(sha256 `184d92a87bdebba1cb5207ee1a111d49aa497f46088cc9f12d9abd3b59c017ec`).

**Status:** `SPEC-READY`

**Raised by:** `tests/results/2026-10-17-live-scan-manual-test.md` — the live UI run
read `raw_cloud num_points=0` on all 3000 ticks, while the post-pause
`get_realtime_model_mesh` snapshot carried the only geometry (5,845 points).

**Method:** static — targeted arm64 disassembly of the C wrapper and its native
chain, plus the temp→show publication functions. Addresses cross-checked against
`.recon-cache/listings/liborbbec_scan_arm64.full.asm`; fresh `llvm-objdump`
excerpts in `.recon-cache/listings/scan-live-data.plan23.asm`. No dynamic tracing;
statements are **[inferred]** from the pinned slice unless tagged otherwise.

---

## 1. The call chain

```
obscan_scan_get_raw_cloud (C wrapper)          @ 0x103f8
  -> OBScanBase::get_raw_cloud                 @ 0x6f91e0   (add x0, x0, #0xf0)
     -> OBScanDataManagement::get_raw_cloud    @ 0x702f3c
```

- The wrapper null-checks the handle → `845283591` (`0x32620107`); otherwise a
  tail call. (`OBScanBase` embeds its data-management subobject at `scan+0xf0`,
  the same subobject the other scan getters use.)
- The native null-checks the out struct → logs `the mesh is nullptr !` →
  `845283584` (`0x32620100`).

## 2. What it reads

- The member at `dataMgmt+0x2e8` is a `std::shared_ptr<rs_common::OBPointCloud>` —
  the **show cloud** (the same member `get_model_cloud` / `get_model_cloud_ptr`
  return; the C API's "raw cloud" *is* this model cloud).
- The read runs under a **byte test-and-set lock at `dataMgmt+0x320`**
  (`mov w10,#1; add x9,x8,#0x320; swpab w10,w11,[x9]; tbnz w11,#0,spin`;
  unlock = `stlrb wzr`, release).
- It writes the caller's `ObScanPointClouds`:
  - `num_points = (points.end − points.begin) / 16` (16-byte points);
  - `points = points.begin`; `normals = [cloud+0x18]` (normals vector begin).
- The cloud object carries three vectors: points `@+0x00`, normals `@+0x18`,
  colors `@+0x30` (the color path reads the third; §4).
- **Borrowed pointers.** No copy, no refcount bump. `obscan_scan_release_raw_cloud`
  is a **no-op stub** (logs, returns `0`; `0x32620107` on a null handle). The
  buffers can be moved or emptied by the next publish/clear (§3) — copy out
  immediately after the call.
- Guard asymmetry, observed: the writers (`update_model_cloud` @ `0x703828`,
  `update_model_cloud_with_bbox` @ `0x703894`) and `get_model_cloud_ptr` take a
  byte lock at `+0x2e0`, while `get_raw_cloud` takes `+0x320` — the guard shared
  by the mesh/marker paths. Both touch the same shared_ptr. Whether that is a
  coarser content lock or a vendor race is not settled here (**OQ-S3**).

## 3. The gate — why the count is 0 during an active scan

The cloud is **double-buffered and published lazily**:

- **Temp cloud** at `scan+0x500` (shared_ptr); byte lock `scan+0x4fb`; bbox at
  `scan+0x510`; published point count at `scan+0x834`.
- **Pending flag** byte at `scan+0x4fa`.
- **Producer** — `OrbbecScanLaser::swap_model_clouds(PointCloudFusionInterface*, bool)`
  (`@ ~0xc71a0`; Speckle sibling touches `+0x4fa` @ `0x5f6b0`): clears the
  pending flag, calls `LaserScanningContextInterface::laser_swap_model(...)` with
  the temp cloud's vectors and `&scan+0x834`, and — only if that returns 0 — sets
  `scan+0x4fa = 1`. Newly fused points/normals land in the **temp** cloud.
- **Publisher** — `OBScanBase::swap_model_temp_to_show_clouds(bbox, bool)`
  @ `0x6f9f04`: reads `+0x4fa`; **if the pending flag is clear it returns without
  doing anything**; otherwise it swaps the temp and show shared_ptrs
  (`update_model_cloud_with_bbox`) and clears the flag.
- **The only callers of the publisher** are
  `OrbbecScanSpeckle::get_model_color_clouds` (`0x5ea84`) and
  `OrbbecScanLaser::get_model_color_clouds` (`0xc5c28`) — i.e. the **color-cloud
  getter** (C API: `obscan_scan_get_model_point_color_clouds`, vtable `+0xe8`).
- `swap_empty_model_clouds` @ `0x6f9e54` (clear/repreview paths) empties the temp
  cloud's vectors and publishes the empty result — this is why the show cloud is
  empty-but-valid at scan start (0 points, no crash).

**Consequence.** `obscan_scan_get_raw_cloud` never triggers a publish. Polling
only it returns the last *published* cloud — empty during a fresh scan — exactly
the manual run's `num_points=0` on all 3000 ticks. The vendor app renders the
color cloud, so its per-frame color getter performs the publish and it sees live
points. This is a **publication-trigger gap in our usage, not a mode/config
gate**: the raw getter has no mode check at all, and the model cloud is fully
populated in the temp buffer while the scan runs.

Writers of the show cloud that are *not* live paths: `update_realtime_scan_result_with_index`
(caller `OBScanBase::edit_delete_post_process_datas_func`), the `import_*`
loaders (`OrbbecScanSpeckle::import_old_realtime_result`,
`..._import_cloud_realtime_stoped_result`, `laser::OBScanLaserMarkerImport`,
`laser::OBScanLaserMarkerlessImport`), `TextureScanningContext::save_model_clouds`,
and `get_realtime_fusion_model` (marker/fusion paths).

## 4. Related surface (for the follow-ups)

- The C API also exposes the color getter `obscan_scan_get_model_point_color_clouds`
  (`0xfba4`; validates the out pointers, dispatches vtable `+0xe8`) and
  `obscan_scan_model_clouds_release` (`0xfc4c`; vtable `+0xf0`) — **neither is in
  our header subset**. The color getter's native gates on the scan state at
  `scan+0x4c8` ∈ 1..9, else `845283600` (`OB_SCAN_ERROR_MULTI_CALLED`,
  `This step can not get model cloud !`).
- `get_realtime_model_mesh` remains pause-gated (the mesh is produced by the
  on-pause optimization) — a separate mechanism, already documented in the header.
- `judge_need_swap_fixed_model` and `get_realtime_fusion_model` are additional
  `+0x4fa` users (marker-framework publication) — not traced.

## OPEN QUESTIONS

- **OQ-S1** — What is `scan+0x4c8` (the state the color getter gates on, 1..9)?
  Map its writers to know when the color getter is legal across the lifecycle.
- **OQ-S2** — Does anything publish temp→show on pause/stop (i.e., would
  `get_raw_cloud` return data after pause *without* the color getter)? `pause`
  clears `+0x4fa` in one branch; no publisher call found in its prologue — not
  exhaustively traced.
- **OQ-S3** — The `+0x2e0` vs `+0x320` guard asymmetry (§2): intentional coarse
  lock or a vendor race? Both paths touch the same shared_ptr.
- **OQ-S4** — The marker-framework `+0x4fa` users (`judge_need_swap_fixed_model`,
  `get_realtime_fusion_model`) — trace when the marker path is specced.
- **OQ-S5** — *Actionable, hardware:* does polling the color getter
  (`obscan_scan_get_model_point_color_clouds`) once per tick make `get_raw_cloud`
  (or the color call itself) return live points during an active scan? Do not
  assume until a run shows it — this is the direct test of §3's mechanism.

## Evidence

- `.recon-cache/listings/scan-live-data.plan23.asm` — curated excerpts: wrapper,
  native getter, writers, both swaps, producer, color getter, C companions.
- `.recon-cache/listings/liborbbec_scan_arm64.full.asm` — full listing used for
  the call-site mapping (addresses above are its).
- Run: `tests/results/2026-10-17-live-scan-manual-test.md` (0 points × 3000
  ticks; 5,845-point mesh after pause; no raw log survives).
