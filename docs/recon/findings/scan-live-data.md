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
not after resume; the mesh must be read **while paused**. A NULL **scan handle**
is the wrapper's `0x32620107` (`OB_SCAN_ERROR_DEADLINE_EXCEEDED`), per the
universal handle rule (`obscan_api.h`). The native's "the laser scan context is
nullptr" branch (`0x42e71d6`) is a **different** check — the internal context
obtained from `scan+0x7b0` via `get_cloud_scan_context()`, reachable only with
step == 3 — and returns MULTI_CALLED. A NULL out struct is `0x32620100` "the mesh
is nullptr !"; mesh-conversion failures are `0x32620101`
`OB_SCAN_ERROR_FATAL`. **[inferred: static writer/gate map; not exercised
per-step on hardware]**

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

## 15. The teardown path and the four RPC client ports (row #4)

*Static half, plan4 pass + the archived runs. Raw excerpts:
`.recon-cache/listings/scan-live-data.teardown-ports.plan4.asm`. The four ports are
`libobsensor::pal::RPCClientPort` objects — they live in **libOrbbecSDK** (the scan dylib
contains no `RPCClientPort` strings); the scan library drives the SDK device that owns
them.*

**The four ports** (per connected device; acceptance-run log): Depth `10010`, IMU
`10011`, RGB `10012`, IR `10013` — the IR port serves IR left+right, IMU serves
accel+gyro.

**What each teardown call releases.**

| call | releases | ports? |
| --- | --- | --- |
| `obscan_scan_pause` | scan state only (step → 3, pending-flag clear) | no |
| `obscan_scan_stop` | the pipeline/streams (`Pipeline destroyed!` during the stop path) | no |
| `obscan_session_destroy` (`0xc1d8`) | the session object only (`can_destroy_this` guard, `set_this_nullptr`, handle cleared) | no |
| `obscan_context_destroy` (`0xb61c`) | the context **and the device**: its success path ends in `bl OBDeviceManagement::release()` (`0xb690`) — the **only caller** of `release()` | **yes — all four** |

`OBDeviceManagement::release()` @ `0x5c14d8` clears the device pointer at manager
`+0x158` and destroys+frees the device (`device::OrbbecDevice::~` @ `0x1b4bbc` →
releases the internal-device shared_ptr), which tears down the SDK device: sensors and
all four ports. `pause`/`stop`/`session_destroy` cannot release the ports — none of them
calls `release()` or `~OrbbecDevice` (the destructor's only callers are `release`,
`OBDeviceManagement::~`, and `init_device`, which destroys the *previous* device on a
device re-init).

**Runtime confirmation (no new run needed).** The acceptance run
(`tests/results/2026-10-17-scan-server-live-acceptance.log`) logs all four
`rpc client destroyed:` lines (RGB, IR, IMU, Depth) within 1.2 ms at teardown
(23:35:37.965), then `Context.cpp:89 Context destroyed`, `obscan_context_destroy ->
OB_SCAN_SUCCESS`, worker exit 0. The same four-line pattern is in the clean-exit
`live_capture_test` run (`logs/branch_claude-clever-albattani-cebx0e-v2/
live_capture_test_run2_CLEAN_EXIT_0.log`).

**Does any path leave one open?** Any path that does not reach
`obscan_context_destroy` — a crash, a bail-out before teardown, a killed process —
leaves the device and all four ports open until process exit (the loader's threading
model already relies on this: threads and device state outlive the destroy call in a
bail-out). On the clean path the stop-path IMU error (`Stop imu stream error:
std::bad_cast` at 23:35:37.584) did **not** prevent the release.

## 16. The stop path's tail — the model save and the .obmc serialization (row #37)

*Static half, plan37 pass. What `obscan_scan_stop` runs after the pipeline is
destroyed, and where the 2026-10-18 scale run's teardown stalled.*

**Call shape.** The tool calls
`obscan_scan_stop(scan, progress_cb, status_cb, /*is_block=*/true)`
(`tools/scan_server/main.cpp:747`). The C wrapper (`0xf980`) registers an
async task (`OBScanBase::begin_async_task`, type `0x10`) and calls
`OBScanBase::stop(progress_cb, is_block)` (`0x6faebc`); with `is_block=true`
the stop runs **synchronously on the calling thread** through the concrete
stop impl (vtable `+0x188`), so the wrapper returns only after the whole tail
below. (`is_block=false` enqueues the same work on the `"default"` named
thread pool and returns; `stop` also `wait_until`s the *previous* future
first, returning `0x32620110` on timeout.)

**The tail, in order** (native log + listing):

1. Pipeline stop + destroy — the `Pipeline.cpp` lines (`Stop pipeline done!` /
   `Pipeline destroyed!`) immediately before the hang.
2. Virtual `save_stop_realtime_scan_datas` — laser override
   `LaserTextureScanning::save_stop_realtime_scan_datas` (`0x9644c8`) →
   `LaserScanningContextInterface::save_stop_realtime_scan_datas` (`0x95f954`):
   `platform::existPath`/`createPath` for the output dir, then two saves.
3. Virtual `save_model_clouds` (`LaserScanningContextInterface`, `0x95f210`;
   one of four overrides — the marker class's is a "not implemented" stub):
   - `obscan_cloud_transform` (`0x6acfe4`) — applies the **rotate pose** to the
     model cloud (transforms into a fresh cloud; vector `__append` + `memcpy`),
     O(vertices);
   - `rs_common::print_matrix("rotate pose for model.ply", pose)` — the bare
     `rotate pose for model.ply` + 4×4 print in the log (~90° about Z in the
     recorded run);
   - `obscan_common::write_cloud_with_intensity_ply(cloud,
     <outdir>/result/model.ply, /*binary=*/1, 1, 0)` (`0x6b9a94`) — binary PLY
     (`format binary_little_endian 1.0`; `property float x,y,z,intensity,nx,ny,nz`),
     **28 B/vertex**, `fopen("wb")` + per-record `fwrite`, with a per-iteration
     `ProgressReporter` check (the "user cancel save !" abort). The first bool
     picks `"wb"` vs `"w"`; the ASCII variant would `fprintf` one
     `"%f %f %f %f %f %f %f %hu %hu %hu\n"` line per vertex;
   - `OBScanDataManagement::update_model_cloud` (27 instrs — a
     `compute_cloud_bbox` pass).
4. `save_detail_model_online_fusion_result` (`0x95fe54`) →
   `OutOfCoreSerializableLaserOnlineFusionOpt::to_buffer(path)` (`0x9a0cbc`) —
   the **out-of-core fusion serialization**: `/temp/mainLOMCache.obmc` (+
   `/temp/detailLOMCache.obmc`; `SerializablePointCloudFuser` /
   `OutOfCoreSerializableOnlineFusion`, guarded by "is not a .obmc file!!!").
5. Then the stop returns and the tool proceeds to `obscan_session_destroy` →
   `obscan_context_destroy` (§15).

**Both runs execute every step** — the pose print is in the clean run too
(its raw log line 4524), straight through to `obscan_scan_stop -> OB_SCAN_SUCCESS`.

**Scale — the model save is linear and completes; the stall is after it.**
The runs' `result/model.ply` under the tool's fixed
`/tmp/openscanner_live_server_session/<session>/` output dir:

| run | model-cloud vertices | file size | header + N×28 | complete | mtime |
| --- | --- | --- | --- | --- | --- |
| clean (01:22) | 225,637 | 6,318,058 | 222 + 6,317,836 ✓ | yes | 01:22:45 |
| rerun (01:47) | 11,398,756 | 319,165,392 | 224 + 319,165,168 ✓ | yes | 01:47:41 |

So the model write **finished** ~12 s into the rerun's stop — it is not the
hang. The last file activity is `temp/mainLOMCache.obmc` at **01:47:43**, then
no logs, prints, or writes for ~6m46s until the launcher's 420 s watchdog
killed the worker (`EXIT_CODE=2`). The stall sits in the post-save tail — the
out-of-core serialization (step 4) is the prime suspect. **OQ-S6** carries the
trace spec.

**Can the tool avoid or bound it?**
- The tail is the SDK's, not something the tool holds: the mesh the tool
  broadcast is a borrowed snapshot it copies out (§14) — holding or dropping
  it changes nothing. The saved model cloud is the SDK's own (11.4M vertices
  here vs the clean run's 225K — ~50×).
- `session_config.output_dir` gates only the `model.ply` write (empty dir →
  the writer fails fast, "save_dir is false") — and that write is not the
  hang, so config avoidance does not dodge it.
- The blocking call is what leaves the tool hostage: the watchdog fires while
  the tool is inside `obscan_scan_stop`. A build-side bound is `is_block=false`
  (returns immediately; the work continues on the `"default"` pool) followed
  by polling the destroys — they refuse (`MULTI_CALLED` / "must first wait
  other process finished") until the tail completes — or a larger teardown
  watchdog. Nothing tool-side skips the SDK's tail.

## 17. The scanner-POV pose channel (row #40)

*Static half, plan40 pass — four read-only sweeps over the existing snapshot
(the pose getters, the markers getter, the camera model, the pose's
publication); no new analysis. Every claim here is `[inferred]` (static-only):
no hardware run has exercised these getters yet.*

### 17.1 The pose getters — `obscan_scan_get_global_pose` / `_get_registration_pose`

| export | wrapper | native chain |
| --- | --- | --- |
| `obscan_scan_get_global_pose` | `0x10308` | `OBScanBase::get_global_pose(OBScanPose_*)` `0x6f90e8` → `this+0xf0` → `OBScanDataManagement::get_global_pose()` `0x703568` |
| `obscan_scan_get_registration_pose` | `0x10380` | `OBScanBase::get_registration_pose(OBScanPose_*)` `0x6f9164` → `OBScanDataManagement::get_registration_pose()` `0x7035d8` |

Both take `(scan, ObScanPose* out)` — a **caller-allocated 128-byte copy**
(contrast the frame payload's `obscan_frame_get_frame_pose`, whose
`ObScanPose**` returns a *borrowed* impl pointer, one-shot, bit 4). The
wrappers log a banner, reject only a NULL scan → `0x32620107` ("the
OBScanHandle is nullptr !"), and tail-call; the natives reject a NULL out →
`0x32620100` ("the global_pose is nullptr !"), copy through a stack temporary
and return 0. **No step gate** — both work in any `OrbbecScanStep`.

**`ObScanPose_` = 128 bytes, sixteen `double`, row-major 4×4.** `m[r][c]` at
`(4r+c)*8`; the translation column is `0x18`/`0x38`/`0x58`; the bottom row is
`[0,0,0,1]` for an isometry. Evidence: the 128-B copies in the getters and
setters (`0x703568`, `0x7035a0`, `0x6f90e8`), the `Isometry3d`↔`OBScanPose_`
round-trip (`0x6acca4`/`0x6acf38`), the 16-double NaN loop (`0x6accc8`), the
Eigen transpose shim (`0x6ace00` ⇒ row-major), and the identity NaN-fallback
(`0x2cf16a0`).

**Storage + sync.** Global pose at `dataMgmt+0x1b0` (`scan+0x2a0`),
registration pose at `+0x120` (`scan+0x210`). Getters and setters all take the
**byte lock at `dataMgmt+0x320`** (`swpab`/`stlrb` — the same guard
`get_raw_cloud` uses), so pose reads are mutually exclusive with writes — not
the family's unsynchronized listener pattern (§11).

**Lifetime**: a value copy into the caller's struct — repeatable, no release,
nothing borrowed.

### 17.2 Publication — event-driven, not per frame

The **global** pose has exactly one writer chain:
`obscan_registration_confirm_or_cancel_result(reg, bool)` (`0xde2c`) →
`OBScanRegistration::confirm_or_cancel_result` (`0x55334`) →
`resolve_registration_result(bool)` (`0x553fc`) — the sole caller of
`OBScanBase::set_global_pose` (`0x6f7f38` → `OBScanDataManagement::set_global_pose`
`0x703530`) — writing the scan's *registration* pose as the new global pose.
Gated on the registration step (`ldar reg+0x50 == 3`, pending set non-empty),
`thread_is_idle`, and the project-step window: **once per confirm/cancel, not
per frame**. The *registration* pose is written by
`OBScanRegistration::auto_regist_func`/`point_pair_regist_func` as the solves
run — so for a live scanner-POV feed the registration getter is the fresher
channel, and the global pose changes only when a result is confirmed. The scan
pose appears in **no listener payload**; the only per-frame pose is the frame
payload's (`FRAME_READY` → `obscan_frame_get_frame_pose`).

### 17.3 The markers getter — `obscan_scan_get_markers`

`(scan, ObScanMarkers* out)`: wrapper `0x105b0` (NULL scan only →
`0x32620107`) → `OBScanBase::get_markers` `0x6f91f0` (a 2-instruction
forwarder, `this+0xf0`) → `OBScanDataManagement::get_markers` `0x703044`:
NULL out → `0x32620100`; takes the same `+0x320` byte lock; copies the
`{count@0x00, markers@0x08, aux1@0x10, aux2@0x18}` view (32 B — layout in
data-process-message-listeners.md §15); unlocks; returns 0.

**Borrowed, not copied**: the three pointers reference the manager's live
container (`*(dataMgmt+0x308)`, a `shared_ptr`); no refcount is taken and the
lock releases *before* return — a concurrent `update_markers_data` (`0x703968`)
can `clear()`+`reserve()` the arrays under the caller. Read synchronously,
don't retain. (Contrast `get_markers_data` `0x7039d4`, the owning
`shared_ptr` variant — **not** what this export uses.)

**Records**: `free_scan::convert_VKeyPoint_to_OBScanMarkersData` (`0x8aba0`;
`VKeyPoint` stride 60 B) fills three parallel per-marker arrays —
`markers`/`aux1` = 16-byte `{3×f32, pad}` records, `aux2` = one `f32` each:
two 3-D points plus a scalar per marker. Field names are not recovered
(OQ-S8). No per-accessor state bit — statically repeatable (OQ-S9).

### 17.4 The camera model — FOV only; no C-reachable intrinsics

`ObScanFov` (0 = NORMAL, 1 = WIDE — the vendor's own name/desc tables
`0x170bec`/`0x170c18`) is already bound, and it is the mode config's
`scan_fov@0x18` (`ObScanModeConfig`, reachable via `obscan_scan_get_mode_config`
`0xff5c`). The vendor's richer camera data (`DeviceParameters`,
`DeCameraIntrinsic_S`, `OBCameraParam`; the `load_camera_params` /
`init_camera_params` import paths `0x9e074`/`0xa1bd8`/`0xa23fc`;
`convert_ferret_camera_params` `0xbd672c`) is **not reachable from the C ABI**;
the only C-reachable parameter path is the opaque device-param blob
(`obscan_device_management_backup_device_param` `0x117b8` /
`resume_device_param` `0x11844`), content unparsed. For the viewer's §5
design: **pose + FOV is everything the pinned ABI supports**; richer
intrinsics need new analysis (OQ-S10 — analysis-needed, deliberately not run).

### 17.5 Error taxonomy (all four exports)

| condition | code |
| --- | --- |
| NULL scan/registration handle (wrappers) | `0x32620107` (`OB_SCAN_ERROR_DEADLINE_EXCEEDED` — the family's generic null-handle code) |
| NULL out (natives) | `0x32620100` (`OB_SCAN_ERROR_INVALID_ARGUMENT`) |
| registration gate fails (`confirm_or_cancel_result`) | `0x32620110` (`OB_SCAN_ERROR_MULTI_CALLED`) |
| success | `0` |

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
- **OQ-S6** — *Actionable, hardware:* where exactly the stop tail stalls at
  scale (§16). The model save completes (both runs' `model.ply` files are
  complete, 28 B/vertex); the last activity is `temp/mainLOMCache.obmc` at
  01:47:43, then silence until the 420 s watchdog. Dynamic trace: reproduce the
  scale run and, during the hang, `sample <worker-pid> 30` (or
  `lldb -p <pid> -o 'thread backtrace all'`) — the stop thread's stack names
  the function (expect `OutOfCoreSerializableLaserOnlineFusionOpt::to_buffer`
  or a wait in the stop impl); plus `lsof -p` (which file it holds), a size
  poll of the `.obmc` (growing = slow, flat = stuck), and `footprint -p`
  (the session's cache is 385 MB here vs 704 MB in the clean run). A
  controlled A/B (same duration, two mesh sizes; or a repeat run) separates
  "slow tail" from "deadlock".
- **OQ-S7** — the pose matrix's convention (§17.1): row-major per the Eigen
  shim, but the rotation's polarity and whether the pose is world←scanner or
  scanner←world are `[inferred]`. Settle with a hardware `get_global_pose` call
  at a known device orientation (compare against the frame pose / the app's
  view).
- **OQ-S8** — the marker record's field names (§17.3): two 3-float triples plus
  a scalar per marker (position? axis? confidence?). Settle by a consumer read
  or a hardware `{count, first record}` log.
- **OQ-S9** — the marker borrow's stability (§17.3): the `+0x320` lock covers
  the copy only; a concurrent marker update can free the arrays. Repeatability
  is statically yes (no state bit). A hardware double-call plus a mid-scan
  re-read would confirm.
- **OQ-S10** — *analysis-needed (serialized by design; do not run without a
  board row):* richer camera intrinsics (§17.4) — whether
  `DeviceParameters`/`DeCameraIntrinsic_S` are recoverable from the snapshot
  and whether any reachable path exposes them. Today only the opaque
  backup/resume blob is C-reachable.
- **OQ-S11** — `obscan_get_marker_mesh` (`0x11ce4`): the exact signature/output
  shape (an int count plus float args and pointers; builds a 72-B-stride
  vertex mesh). A separate contract row if the marker mesh is wanted.
- **OQ-S12** — the pose's publication trigger `obscan_registration_confirm_or_cancel_result`
  (`0xde2c`) is not in our header subset (§17.2): the viewer's `pose` message
  depends on it (the global pose updates per confirm). Declare it in a
  registration-family row, or document the registration getter as the live
  channel.

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

Row #37 (the stop-tail hang): `tests/results/2026-10-18-scan-server-scale-acceptance-rerun.md`
(the hung run) and `tests/results/2026-10-18-scan-server-scale-acceptance.md` (the clean
contrast); the tool's stop call site `tools/scan_server/main.cpp:747`; and the on-disk
artifacts under `/tmp/openscanner_live_server_session/` — `sjNH28m_…/result/model.ply`
(319,165,392 B, complete, mtime 01:47:41) and `sjNH28m_…/temp/mainLOMCache.obmc`
(385,871,872 B, mtime 01:47:43) vs the clean run's `s40lAcB_…/result/model.ply`
(6,318,058 B, complete) and `s40lAcB_…/temp/mainLOMCache.obmc` (704,504,982 B).

Row #40 (the scanner-POV pose channel): four read-only sweeps over the existing
snapshot (pose getters, markers, camera model, pose publication) — no analyzer run.
Targets/addresses as cited in §17; the sweeps' raw read ranges are recorded there and
in the four draft files (deleted after consolidation, their substance is §17).
