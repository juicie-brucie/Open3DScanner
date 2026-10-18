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

## 4. The color getter and its release — now in the header subset (row #24)

- `fn_obscan_scan_get_model_point_color_clouds` (wrapper `0xfba4`; native
  `OrbbecScanLaser::get_model_color_clouds` @ `0xc5bdc`, dispatched through
  vtable `+0xe8` from the vptr) and `fn_obscan_scan_model_clouds_release`
  (native `OrbbecScanLaser::set_update_model_release` @ `0xc5cc4` — **a bare
  `ret`**: the release frees nothing; the wrapper returns `0` for any valid
  handle) are declared in `obscan_api.h` as of this pass.
- C shape: `(scan, out_clouds, out_bbox, out_count)`. The wrapper validates
  `out_clouds` (`the OBScanPointClouds is nullptr !`) and `out_bbox` (`the
  OBScanCloudBbox is nullptr !`) — both `845283584` — but **not** `out_count`;
  pass a valid pointer. `out_count` receives `scan+0x834`, the fusion's
  accumulated point count.
- `ObScanCloudBbox` (48 bytes) is declared alongside: min/max/center float
  triples with a pad word after each; center = (min+max)/2. Layout from
  `obscan_common::compute_cloud_bbox` @ `0x6ad464` / `init_obscan_bbox`
  @ `0x6ad540`; the getter copies it from `dataMgmt+0x250` (the published
  cloud's bbox).
- `get_realtime_model_mesh` remains pause-gated (step == 3, §5) — the mesh is
  produced by the on-pause optimization, a separate mechanism.
- `judge_need_swap_fixed_model` and `get_realtime_fusion_model` are additional
  `+0x4fa` users (marker-framework publication) — not traced.

## 5. The scan step at `scan+0x4c8` (OQ-S1, answered)

`OBScanBase::get_obscan_step` @ `0x6f98a8` reads a 4-byte step value at
`scan+0x4c8` (`ldar` acquire); transitions are `stlr` release stores. Values
observed at their writers:

| value | meaning (from the writer) |
| --- | --- |
| 1 | ready / preview (`preview`, `clear_repreview` end, `reconfig_*`, `clear_point_cloud_impl` end) |
| 2 | **scanning** (`start` — both scan classes) |
| 3 | paused (`pause`, `device_disconnect_pause`, `load_cache_file`) |
| 4 | stopped (`after_stop_process_func`, `clear_point_cloud_impl` initial) |
| 5 | post-process / optimization-to-mesh (`edit_delete_post_process_datas_func`, `judge_optimization_to_mesh_problem_state`, `send_optimization_to_mesh_failed_msg`) |
| 6 | optimization-to-mesh (`OBScanBase::optimization_to_mesh`) |
| 9 | mesh-adjacent (`OrbbecScanLaser::after_stop_process_func`, one branch) |
| 10 | meshlization (`OBScanBase::meshlization`) |
| 16 | texturization (`OBScanBase::texturization`) |

Gates: `get_model_color_clouds` requires **1..9** (else `845283600`,
`OB_SCAN_ERROR_MULTI_CALLED`, `This step can not get model cloud !`);
`get_model_mesh` requires **== 3** — this is the header's "only valid while
paused" note; `is_realtime_scanning_state` is true for **≤ 3** (the "must
finish current scan" gate). An active scan sits at step 2, inside the getter's
window — so the manual run's 0 points were the publish-trigger gap, never a
step rejection. **[inferred: static writer/gate map; not exercised per-step on
hardware]**

## 6. Live-path decision — getter-publish is ours; the listener stays a frame channel

*(Roadmap rev-5 seed 1: read §3 against `docs/research/pipeline-quality-assessment.md`
§1.3/§1.5 — unreviewed — and the header; pick the path, record the decision and the
loser's prerequisite.)*

Evidence re-checked against the archived app session
(`logs/creality_scan_comparison_20261016/native_logs/scan_log_20261016-024738.txt`) and
our own run (`logs/exposure_sweep_new_setup_20261016/native_scan_log.txt`).

**§1.3 "the real app never polls for the live cloud" — adopted, one correction.**

- The app registers **all five listener families** at startup (02:47:38:
  `obscan_context_add_data_msg_listener`, `add_process_msg_listener`, control, device,
  tips — all in the same second). The corpus did not cite this; it is the strongest
  single support for "the listener is the app's channel".
- The four getters the corpus names appear in **two bursts only** (the log contains
  exactly one cloud *getter* — `obscan_scan_get_raw_clouds`, called in both bursts) — at scan creation
  (02:48:15.15) and once mid-session (02:49:00.73, inside a UI state-refresh sequence
  ending in `obscan_scan_plane_remove(false)`). Not "exactly once" — but the thrust
  holds: **no per-frame polling**.
- "Live frames reach it through the data-message listener" **cannot be shown in the
  app's log and is not disproved by it either**: `send_data_msg` is **silent**
  (drop-at-emit, no log line), so the log has no visibility into data deliveries. The
  claim rests on the registration above + the never-polled getters + the recovered
  per-frame ids (`FRAME_READY`, `NEW_{IR,DEPTH,RGB}_IMAGE`). **[inferred]**
- Caveat: the log only proves what went through the `obscan_*` C wrappers; direct
  native calls (IL2CPP bindings) would not appear. Nothing observed suggests it, but
  the log cannot exclude it. **[inferred]**

**§1.5 "our tooling drives the SDK differently" — confirmed in part, corrected in part.**

- Confirmed: we register the data listener (03:06:48) and never consume its payloads;
  our runs emit `frame queue is full` (907 in the exposure-sweep run) and the app's log
  has **zero** `frame` warnings. (The `m` in `mframe`/`mdepth`/`mir` is the SGR
  terminator of the preceding escape — the messages are `frame`, `depth`, `ir`.) That is the corpus's sharpest claim and it holds.
- Corrected: "a `frame queue is full` warning the real app never emits" is true only for the
  `frame` queue — the app's own log carries **5,196** queue-full warnings (2,598 `depth` +
  2,598 `ir`). Our own counts are `frame` 907, `depth` 1,062, `ir` 1,062 — 3,031 in
  total, independently reproducing the corpus's "about 3,000 in a 30-second run" — and
  the distinction is the `frame` queue only (907 against the app's 0), not queue-full
  warnings in general. The queues overflow in the vendor app too.
- Corrected/tension: the causal story ("we never drain → the queues fill") is not
  established and sits in tension with the same corpus's §1.1 ("queue is full = the
  paused state, not backpressure"). Timings fit neither cleanly: our warnings start
  ~60 ms **before** `obscan_scan_start` (stream/preview start) and run continuously;
  the app's start right after its first pause. Once full, they stay full in both.
  **[inferred]**
- The data emitter has **no direct `bl` callers**; it is referenced from data-section
  tables (4 pointers) — the emitters are reached indirectly. Naming them and the
  payloads is **OQ-P3 (#28)**.

**Decision.** *Getter-publish is our live path.* Per tick,
`obscan_scan_get_model_point_color_clouds` publishes the pending temp→show swap and
returns the model cloud (points, normals, colors, bbox) — the exact artifact our UI
renders, with no payload decoding and no extraction work. Implemented (#25), statically
sound (§3), hardware proof pending (OQ-S5). *The listener channel is not our live-view
path*: it delivers per-frame notifications/images, not a cloud; using its payloads needs
OQ-P3, and turning raw images into a cloud is the SDK's own extraction — which the same
corpus (§2) says to keep behind the SDK.

**The loser's prerequisite.** The listener stays: (i) mandatory to register (the preview
gate, #2); (ii) worth **draining** for queue hygiene and future per-frame features
(G1/G2) — independent builder work; (iii) **OQ-P3 (#28)** gates *using* its payloads.

**Revisit condition — FIRED (plan28 pass, OQ-P3 answered).** `FRAME_READY`'s `data` is an
`OBScanFrame_*`, and the exported `obscan_frame_get_frame_cloud` returns the frame's
per-frame point cloud (one-shot; see
[`data-process-message-listeners.md`](data-process-message-listeners.md) §12). The
listener channel therefore **can** carry per-frame clouds, so the decision should be
re-taken: the listener path is per-frame (lower latency, vendor-faithful) but is not the
accumulated model cloud this UI renders — an accumulated view would need our own
accumulation, and the `obscan_frame_*` C surface is not in our header subset yet.
Getter-publish (#25) stands until the re-take.

**Corpus status.** `docs/research/**` stays frozen; this section supersedes §1.3/§1.5
where they differ (the two-burst correction, the 5,196-warning correction, the causal
caveat).

## 7. The raw/color lockstep is the mechanism, exactly (row #30)

#25's acceptance run (`tests/results/2026-10-17-scan-server-live-acceptance.md`) logged
`raw_cloud num_points` **exactly equal** to `color_cloud num_points` at every sampled
tick (292/292, 340/340, 442/442, 867/867, 883/883), unlike #23's flat 0. Candidate (a)
is the answer, and it is a **structural consequence** of §2/§3 — not a coincidence and
not a new mechanism.

**The trace, end to end.**

1. `get_raw_cloud` has no publish path and no gate of its own (§2): it locks
   `dataMgmt+0x320`, reads the show-cloud shared_ptr at `dataMgmt+0x2e8`, copies
   `count = (points.end − points.begin)/16`, `points.begin`, `normals.begin`, unlocks.
   No calls, no flag access.
2. `get_model_color_clouds` (§3/§4) calls `swap_model_temp_to_show_clouds` (the
   flag-gated temp→show publish) **first**, then reads the same member via
   `get_model_cloud_ptr`. After the color call, the show cloud is current.
3. The tool calls them in that fixed order every tick — color, then raw, then logs both
   counts from the same tick (`tools/scan_server/main.cpp` ~607–659; the raw call was
   added back alongside the color call by PR #67's fix-forward `a88d576`, whose own
   comment reads "Raw getter never publishes; reading it after the color getter shows
   the last published cloud"). Raw therefore reads the very cloud the color call just
   published, and both counts come from the same member via the same formula —
   **necessarily equal**. (The record's "within 1–2" belongs to the max-seen counter,
   not to the pair.)
4. No swap can intervene between the two calls: the only swap triggers are the getter
   calls themselves (the clear paths that also publish do not run during scanning), and
   the tool's calls are single-threaded. The fusion producer only fills the temp cloud
   and sets the pending flag; it never swaps.

This also demonstrates §2's contract in the field: `get_raw_cloud` returns exactly the
last published cloud.

**Candidates (b) and (c).** (b) is subsumed, with a correction: the difference from #23
is the **presence** of the color call, not the order — pre-#25 the loop polled raw only
(that is the flat-0 record); PR #67 replaced it with the color call; the fix-forward
added raw back for contrast. Any order still yields equality as long as both reads
follow a publish — the order only decides same-tick vs one-tick-stale. (c) is ruled out:
both records ran the same tool (`scan_server --live`, default SDK resolution — the
acceptance record documents `/Applications/CrealityScan.app/.../liborbbec_scan.3.5.3.dylib`,
sha `184d92a8…c017ec`); the only variable that changed between the records is the tool.

**Is an isolated raw-only run needed? No.** That case is already recorded: the pre-#25
run (`tests/results/2026-10-17-live-scan-manual-test.md`, same dylib, raw-only loop)
returned 0 for all 3000 ticks. The two records plus the static trace form the complete
A/B. A same-build raw-before-color ordering experiment would only show the lag semantics
already implied here; if anyone still wants it, it is a tool change (builder), not recon
or hardware.

## 14. The realtime-mesh getter's lifetime rules (row #3)

*Static half, plan3 pass. Native: `OrbbecScanLaser::get_model_mesh` @ `0xc5cc8`
(wrapper `0xf304` → vtable `+0xf8`); release `OrbbecScanLaser::model_mesh_release`
@ `0xc5fe8` (wrapper `0xf384` → vtable `+0x100`). Raw excerpts:
`.recon-cache/listings/scan-live-data.mesh-lifetime.plan3.asm`.*

**When it is valid.** The first instruction pair reads the scan step
(`scan+0x4c8`, `ldar`) and requires **exactly 3 (paused)** — anything else logs
"get model mesh must first pause scan !" (`0x42e722b`) and returns `0x32620110`
(`OB_SCAN_ERROR_MULTI_CALLED`). So: not during scanning (2), not after stop (4),
not after resume; the mesh must be read **while paused**. (The null-context path
is also MULTI_CALLED; a NULL out struct is `0x32620100` "the mesh is nullptr !";
mesh-conversion failures are `0x32620101` `OB_SCAN_ERROR_FATAL`.)

**It is a build, not a copy.** Each call:

1. allocates a **552-byte mesh kernel** (`operator new(0x228)`,
   `bc102fae::AttribKernel<FinalMeshItems<DefaultTraitsDouble>, TriConnectivity>`),
   stores it at **`scan+0x9a8`**, and destroys the previous kernel there;
2. fills it from the laser scan context (`scan+0x7b0` →
   `LaserScanningContextWrapper::get_cloud_scan_context` →
   `LaserScanningContextInterface::get_model_mesh` @ `0x95b684`; a second variant
   is appended when the extern-config word has bit `0x18` set);
3. converts it into the **`OBScanMeshDatas` cache at `scan+0x9a0`**
   (`libpp::convert_openmesh_to_obmesh` @ `0x76386c`; `_append` @ `0x763f14`) —
   allocated once (120 bytes) and **re-filled/overwritten on every call**;
4. writes the out struct from the cache: `num_vertices@0`, `vertices@8`,
   `normals@0x10`, `num_triangles@0x18`, `triangles@0x20` — **borrowed pointers
   into the cache**, no copy, no refcount.

**Repeated calls / invalidation.** Because each call rebuilds the kernel and
re-fills the datas, the pointers returned by a previous call are invalidated
(and their memory may be reused). Consume or copy out before the next call.

**Ownership and free points.**

- `obscan_scan_realtime_model_mesh_release` → `model_mesh_release` @ `0xc5fe8`:
  **frees the datas cache** (`OBScanMeshDatas` destructor + `operator delete`;
  clears `scan+0x9a0`) and returns 0 — a real release, unlike the cloud
  release's bare `ret`.
- The kernel at `scan+0x9a8` is *not* freed by the release: it is freed by the
  next getter call, by `clear_point_cloud_impl` / `clear_repreview`
  (`0xcd17c` / `0xc5474` — free and clear `+0x9a8`), or by the scan destructor
  (`0xc98bc`, which frees both).
- The datas cache is additionally freed by the scan destructor; `+0x9a0` is
  created lazily by the first getter call.

**Dynamic trace: not needed** — all rules are instruction-level; the only
runtime-dependent facts are the mesh contents/counts, which the acceptance run
already sampled (6112 vertices / 4331 triangles after pause).

## OPEN QUESTIONS

- **OQ-S1** — *Answered* (row #24) → §5. `scan+0x4c8` is the `OrbbecScanStep`
  (1 ready, 2 scanning, 3 paused, 4 stopped, 5/6 post-process, 9 mesh-adjacent,
  10 meshlization, 16 texturization); the color getter is legal for 1..9, the
  mesh getter for == 3.
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
- `.recon-cache/listings/scan-live-data.plan24.asm` — row #24's additions: the
  color-getter wrapper, vtable mapping, the `ret` release native, the step
  writer/gate map, and the bbox layout.
- Logs re-read for §6: `logs/creality_scan_comparison_20261016/native_logs/scan_log_20261016-024738.txt`
  (the vendor app's session — listener registrations, getter bursts, queue-full counts)
  and `logs/exposure_sweep_new_setup_20261016/native_scan_log.txt` (our run).
- §7 re-read: `tests/results/2026-10-17-scan-server-live-acceptance.md` + its
  `.log` (the lockstep ticks), and `tests/results/2026-10-17-live-scan-manual-test.md`
  (the pre-#25 raw-only control).
- `.recon-cache/listings/liborbbec_scan_arm64.full.asm` — full listing used for
  the call-site mapping (addresses above are its).
- Run: `tests/results/2026-10-17-live-scan-manual-test.md` (0 points × 3000
  ticks; 5,845-point mesh after pause; no raw log survives).
