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
- **Guard asymmetry — answered (row #67): a vendor race, not a coarse lock.**
  The model-cloud slot (`dataMgmt+0x2e8`/`+0x2f0`) has its own byte lock at
  `dataMgmt+0x2e0`, and the vendor's whole internal model-cloud family uses it
  coherently: `get_model_cloud` (`0x703934`), `get_model_cloud_ptr`
  (`0x7038e8`), `update_model_cloud` (`0x70383c`, the two-store swap of
  `+0x2e8`/`+0x2f0`) and `update_model_cloud_with_bbox` (`0x703898`). The
  **C-facing `get_raw_cloud` takes the class-wide `+0x320` instead**
  (`0x702f50`) — so it does *not* exclude the writers: none of the 13
  `update_model_cloud` callers holds `+0x320` before the call
  (`get_realtime_fusion_model`, the import paths,
  `update_realtime_scan_result_with_index`, the three `save_model_clouds`, the
  four `remove_plane_cut_model`s). A concurrent publish can therefore be
  observed **torn** by `get_raw_cloud` (new pointer with the old control
  block), or free the old cloud under the reader's borrowed pointers — the
  same hazard class as the markers' borrow (OQ-S9). **Mitigation (already the
  rule): copy out immediately; never retain the borrowed pointers.** Watch
  signature if it ever surfaces in hardware: a `num_points` that disagrees
  with the copied points, or a fault in the copy loop while a
  plane-cut/optimize/fusion publish is running.

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
destroyed, and why the 2026-10-18 scale run's teardown did not fit its
watchdog budget.*

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

**Scale — the model save is linear and completes; the tail is what ran out of budget.**
The runs' `result/model.ply` under the tool's fixed
`/tmp/openscanner_live_server_session/<session>/` output dir:

| run | model-cloud vertices | file size | header + N×28 | complete | mtime |
| --- | --- | --- | --- | --- | --- |
| clean (01:22) | 225,637 | 6,318,058 | 222 + 6,317,836 ✓ | yes | 01:22:45 |
| rerun (01:47) | 11,398,756 | 319,165,392 | 224 + 319,165,168 ✓ | yes | 01:47:41 |

So the model write **finished** ~12 s into the rerun's stop — the save is not
the problem. The stop's tail then continued into the out-of-core serialization
(step 4), and that is where the run ended: `temp/mainLOMCache.obmc`'s last
write is **01:47:43** — the same second the process exited — so the
serialization was **cut off mid-tail** by the launcher's watchdog, not a stall
after the save. The watchdog is `session_seconds + 120` = **420 s measured
from `fork()`** (`engine/src/worker_process.cpp`: the parent enters its 100 ms
poll loop immediately after the fork and `SIGKILL`s on expiry; the run's
record states the same 420 s figure). The run's arithmetic: fork ~01:40:33;
the live loop ~332 s; the pause → 15.5M-vertex mesh → broadcast → stop
preamble the next ~75–85 s (pipeline destroyed 01:47:29); leaving the stop
tail only **~14 s** — enough for the model save (~12 s) but not for the
`.obmc` serialization behind it. **The tail did not fit the budget.** **OQ-S6**
carries the trace spec.

**Can the tool avoid or bound it?**
- The tail is the SDK's, not something the tool holds: the mesh the tool
  broadcast is a borrowed snapshot it copies out (§14) — holding or dropping
  it changes nothing. The saved model cloud is the SDK's own (11.4M vertices
  here vs the clean run's 225K — ~50×).
- The binding constraint is the **budget**, not a stall: the watchdog's 420 s
  starts at `fork()`, so the tool's own phases spend it first — here ~332 s of
  live loop (32 s over nominal: the ticks averaged ~0.11 s) plus the ~75–85 s
  preamble (pause → 15.5M-vertex mesh extraction → 15.5M-point broadcast → the
  fixed 0.5 s + 5 s waits) left only ~14 s of the 120 s teardown allowance.
  Ending the live loop early, trimming the preamble, or widening the margin
  each buys the tail budget.
- `session_config.output_dir` gates the `model.ply` write (empty dir → the
  writer fails fast, "save_dir is false"). That write was ~12 s of the tail's
  ~14 s, so skipping it buys real budget — but the `.obmc` serialization behind
  it is unconditional.
- `is_block=false` returns immediately (the work continues on the `"default"`
  pool), but the destroys then refuse (`MULTI_CALLED` / "must first wait other
  process finished") until the tail completes and the watchdog still fires —
  it buys responsiveness, not exit. Nothing tool-side skips the SDK's tail.

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

**Convention (OQ-S7, answered statically)** — the pose is the scanner's pose
in the world: `p_world = M · p_scanner`, applied **as-is** (never inverted).
Evidence: `obscan_cloud_transform`'s per-point math (`0x6ad174`–`0x6ad1c0`:
`out[r] = m[r][0]·x + m[r][1]·y + m[r][2]·z + m[r][3]` — the `m3f::inverse`
in that function is on the normals path), `OBPointCloud::transform`
(`0x26e7620`) likewise, the Eigen shim (`0x6ace00`) transposing *storage* only
(no semantic flip), and a consumer (`0x8d7fd4`) that treats the translation as
the sensor origin. So the translation column is the scanner origin in the
world (project units — metres `[inferred]`), and the rotation is a proper
right-handed rotation whose columns are the scanner axes in world coordinates.
Residual: the optimizer's own sign convention
(`OrbbecScanCombination::optimization` `0x56570` → `set_registration_pose`
`0x56620`) and the world up-axis/units are not statically pinned — one
`get_global_pose` at a known device pose settles them (OQ-S7).

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

**The trigger is a real export (OQ-S12, resolved) — and now declared.** The
publication path is a complete exported family:

- `obscan_session_create_registration(session, ObScanRegistrationHandle* out)`
  (`0xda1c` → `bbe4e2e6::create_registration(OBScanRegistrationHandle_**)`
  `0x22538`; NULL session → `0x32620107`). The handle is opaque and wraps the
  object — `destroy` reads its inner pointer at `+0x00`.
- `obscan_registration_confirm_or_cancel_result(handle, bool confirm)`
  (`0xde2c`, logs `obscan_registration_confirm_or_cancel_result: %s`; NULL
  handle → `0x32620107`) → `OBScanRegistration::confirm_or_cancel_result(bool)`
  (`0x55334`) → `resolve_registration_result(bool)` (`0x553fc`) — the sole
  caller of `set_global_pose`. **Confirming publishes the scan's registration
  pose as the global pose; cancelling drops the pending result.** The gates
  (the registration step `reg+0x50 == 3`, the pending set, `thread_is_idle`,
  the project-step window → `0x32620110`) are the `[inferred]` half from the
  row-#40 sweep.
- `obscan_registration_destroy(handle)` (`0xdecc`; NULL handle or NULL inner
  object → `0x32620107`; gated by `can_destroy_this` `0x55710`).

**What a consumer can rely on:** the *registration* pose is the live solve
(`obscan_scan_get_registration_pose`) — the channel V4 uses; the *global* pose
(`obscan_scan_get_global_pose`) changes only when a registration result is
confirmed through the trigger above. So a viewer's `pose` message can be fed
getter-only (the registration getter, per frame) or "confirmed" (the global
getter, on the trigger) — the header now declares both.

The family is larger than the trigger: the runners
`obscan_registration_auto_registration` (`0xda94` →
`OBScanRegistration::auto_registration(OBScanIndexs_ const&, std::function…)`
`0x53e90`) and `obscan_registration_point_pair_registration` (`0xdc5c` →
`…::point_pair_registration(OBScanPPRegist… const&, …)` `0x54be0`) with their
cancels (`0xdbec`/`0xddbc`), the session group-info getters
(`0xc908`/`0xc980`), and the reference-rod register/unregister trio
(`0x128a8`/`0x12920`/`0x12998`). Their multi-argument signatures are **not**
read here — a follow-up row if the viewer must *run* a registration, not just
confirm one.

### 17.3 The markers getter — `obscan_scan_get_markers`

`(scan, ObScanMarkers* out)`: wrapper `0x105b0` (NULL scan only →
`0x32620107`) → `OBScanBase::get_markers` `0x6f91f0` (a 2-instruction
forwarder, `this+0xf0`) → `OBScanDataManagement::get_markers` `0x703044`:
NULL out → `0x32620100`; takes the same `+0x320` byte lock; copies the
`{count@0x00, markers@0x08, aux1@0x10, aux2@0x18}` view (32 B — layout in
data-process-message-listeners.md §15); unlocks; returns 0.

**Borrowed, not copied — and the borrow can dangle (OQ-S9, answered).** The
three pointers reference the manager's live container (`*(dataMgmt+0x308)`, a
`shared_ptr`); no refcount is taken and the lock releases *before* return. The
update path (`update_markers_data` `0x703968`, verified in the binary) **swaps
the whole `shared_ptr` pair** under the same lock — no in-place mutation — and
the old container returns to the producer's temporary, which frees it on the
producer thread. A concurrent update during an active scan therefore frees a
retained view (use-after-free): read synchronously, copy out, don't retain.
The container is allocated eagerly by the base constructor (`0x701098`), so a
scan with no markers yet returns `{0, NULL, NULL, NULL}`, not a crash. No
per-accessor state bit — the getter is repeatable. (Contrast `get_markers_data`
`0x7039d4`, the owning variant that copies and refcounts — **not** what this
export uses, and not C-exported.)

**Records (OQ-S8, answered).** The three arrays are parallel per-marker arrays
over `rs_common::VKeyPoint` (60 B): `markers` = 16-byte `{x, y, z, pad}`
positions (VKeyPoint+0x00), `aux_0` = the same shape — the **normals**
(VKeyPoint+0x28), `aux_1` = one `f32` — the **radius** (VKeyPoint+0x34).
Anchored by the vendor's own framework schema string `"##### Dim: 7 (x, y, z,
normal_x, normal_y, normal_z, radius)"` (`0x6d594c`+0x1b9), the mm→m scaling
applied to the position and radius only, and the serializers
(`SerializableMarkerPoints` magic `0x103` persists position + normal + radius +
state; the local-frame `0x104` also persists `+0x0c..+0x20`; `+0x24` is
persisted by neither ⇒ padding). Residual (OQ-S8): the `+0x0c..+0x18` ellipse
coordinate fields and the `+0x1c`/`+0x20` ints (a `-1` sentinel in the stereo
path) are unmapped — not needed by the exposed arrays; `VKeyPoint+0x38` is a
`marker_state` bitfield (bit0 deleted, bit1 imported, bit2 hidden — accessors
`0x6a8dc0`/`0x6a8dd8`/`0x6a8df0`), not exposed through `ObScanMarkers`.

### 17.4 The camera model — no C getter, but a decodable session record (OQ-S10)

**No C export returns intrinsics or distortion as a value.** The vendor's
`DeviceParameters` getter (`device::OrbbecDevice::get_device_parameters`,
`0x1b4bc0`) is internal — its callers are the scan-side save/init paths
(`OrbbecScanSpeckle`/`OrbbecScanLaser::save_camera_intrins`,
`…::save_ext_params`, `…::judge_device_params`,
`TextureScanningContext::init_scanning_params`, …) — and the export surface
has no getter for it.

**But the ABI does open a path — the session's project cache.** At preview /
new-scan start the SDK runs `save_camera_intrins` (callers: `preview`,
`new_scan_preview`, `clear_repreview`, `reconfig_texture_config`,
`framework_context_to_speckle_context`), which fetches the `DeviceParameters`
and stores them under the record name **`camera_intrin`** via
`SerializableDeviceParams` (`0x6e9000`) → `SerializableDeviceParams::to_buffer`
(`0x6e3874`) → `rs_common::ProjectFileCache::store_serializable` (the read side
is `OBScanSerialIO::load_device_params_data` `0x6eb98c`, used by the
project-import paths `0x9e074`/`0xa1bd8`). That cache is the session's
**`<session output_dir>/resources.obscan` — a SQLite 3 database** (`files` /
`editDatas` tables; the `-wal`/`-shm` companions are SQLite's). The record is
the row **`files.name = 'camera_intrin.bat'`** — verified empirically in the
row #32 clean run's session dir (`length(data) = 210`). A consumer holding the
session dir (or an export via `obscan_session_export` `0xd088`) can therefore
read the intrinsics **with any SQLite client**.

**The record's schema is positional, not self-describing.** `to_buffer` packs a
fixed sequence of **41 fields** (25×f32, 9×i32, 1×u32, 1×u64, 6
length-prefixed strings) over the `DeviceParameters` struct, each value written
**big-endian** (`pack_bytes_pod<float>` `0x1a03f4` stores byte 3 first, with a
bounds check). The colour intrinsics sit in packed positions 11–16 (f32 ×4 at
`DeviceParameters+0x1c..+0x28`, then 2×i32 at `+0x2c/+0x30`), anchored by
`device::AbstractDeviceHandle::copyColorParams` (`0x317688`: 4×f32 → `+0x1c`,
2×int16 → `+0x2c`, Brown distortion 8×f32 → `+0x34/+0x44`); the depth/D2C/
camera blocks and version strings follow (offsets `+0xa28..+0xb08`).

**The backup/resume blob is not the path — and not decodable here.** The C
wrapper `obscan_device_management_backup_device_param` (`0x117b8`;
`(handle, uint8_t** out, uint32_t* len)`) → `OBDeviceManagement` (`0x5c4ce4`,
gated on the manager's device-present flag `+0x165`) → `OrbbecDevice` →
`OrbbecInternalDevice::backup_device_param` (`0x1e20dc`), which dispatches to
the **device handle's own virtual** (`vtable+0x200`, `0x1e2154`) —
implemented in **libOrbbecSDK**, outside the pinned snapshot. Its content and
schema cannot be settled from this binary; it stays an unproven candidate
(settle with the libOrbbecSDK binary or a runtime hexdump of the blob).

Residual (OQ-S10): the byte-exact position→semantic map of all 41 packed
fields (the colour-intrinsics block is anchored; the identity/version fields
are not).

### 17.5 Error taxonomy (all four exports)

| condition | code |
| --- | --- |
| NULL scan/registration handle (wrappers) | `0x32620107` (`OB_SCAN_ERROR_DEADLINE_EXCEEDED` — the family's generic null-handle code) |
| NULL out (natives) | `0x32620100` (`OB_SCAN_ERROR_INVALID_ARGUMENT`) |
| registration gate fails (`confirm_or_cancel_result`) | `0x32620110` (`OB_SCAN_ERROR_MULTI_CALLED`) |
| success | `0` |

### 17.6 `obscan_get_marker_mesh` — the adjacent, unbound export (OQ-S11)

`0x11ce4` is the export itself (the generator inlined; its only calls are
malloc/bzero/free, `sincosf` and `std::__throw_length_error`). It builds an
**n-segment disc/fan** (`2πi/n`, `sincosf`) oriented by a float/vector argument
block into **two caller buffers** — `out_a` at 36 B per segment (9 `f32`, a
triangle per segment) and `out_b` at 72 B (18 `f32`) — allocates an internal
24·n-byte scratch that it frees before returning, and returns `0` (no pointer).
`n == 0` succeeds silently; `n < 0` throws a C++ `std::length_error` (not an
ErrCode). The earlier "72-byte vertex mesh with a fixed-resolution primitive"
reading is superseded. Gap: the float/vector argument block's lane roles are
not statically derivable — an lldb/dtrace capture of the entry registers
(`$w0,$x1,$x2,$v0.4s,$v1.4s,$v2.4s`) plus a small-`n` hexdump of both buffers
settles it. Its caller is unproven: the name is absent from the app's P/Invoke
tables. It stays undeclared (a separate contract row if wanted).

## 18. The config apply surface — post-creation setters (row #55)

*Static half, plan55 pass (one sweep + inline reads; no analyzer). The row's
premise — "no mode/FOV setter" — is superseded: the export table carries a
family of setters, including the full mode-config re-apply.*

### The C-reachable setters (all reject a NULL scan with `0x32620107`)

| export | C signature | native chain | mutates |
| --- | --- | --- | --- |
| `obscan_scan_config_combined_exposure_and_laser_current` (`0xedcc`) | `(scan, int32_t)` | `OBScanBase::config_combined…` `0x6f691c` | pushes to the device; on ok writes `dataMgmt+0x40` |
| `obscan_scan_config_ir_exposure_time` (`0xee54`) | `(scan, int32_t)` | vtable `+0x28` → `…config_ir_exposure_time(int)` `0x6f5a0c` → `OBDeviceManagement::set_ir_exposure_time` `0x5c6400` | the device (live) |
| `obscan_scan_config_color_exposure_time` (`0xeee4`) | `(scan, int32_t)` | vtable `+0x30` → `…config_color_exposure_time(int)` `0x6f5a78` | the device |
| `obscan_scan_config_depth_range` (`0xef74`) | `(scan, int32_t, int32_t)` | vtable `+0x38` → `…config_depth_range(int, int)` `0x6f5b00` | the device / streamer |
| `obscan_scan_config_laser_current` (`0xf018`) | `(scan, int32_t)` | vtable `+0x48` → `…config_laser_current(int)` `0x6f5c10` | the device |
| `obscan_scan_config_rgb_fill_light` (`0xf0a8`) | `(scan, bool)` | `OBScanBase::config_rgb_fill_light(bool)` `0x6fa630` | the device |
| `obscan_scan_reconfig_scan_mode_config` (`0xf148`) | `(scan, const ObScanModeConfig*)` | prints the config, copies it, vtable `+0x78` → `OrbbecScanSpeckle/Laser::reconfig_scan_mode_config(OBScanModeConfig, bool=1)` `0x5cb00`/`0xc41a4` | the stored `OBScanModeConfig` (`dataMgmt+0xa8` via `set_mode_config` `0x702504`) **and** the device (`streamer_set_scan_config`) |
| `obscan_scan_config_show_data_rotate` (`0xf1f0`) | `(scan, bool)` | `OBScanBase::config_show_data_rotate(bool)` `0x6f7dc8` | a view flag |
| `obscan_scan_config_light_type` (`0xf268`) | `(scan, ObScanLightType)` | vtable `+0x70` → `…config_light_type(int, bool=1)` `0x6f5c84` | the mode/device |
| `obscan_scan_config_manaul_remove_plane` (`0xf8cc`) | `(scan, const ObScanManualRPConfig_*)` | `…config_manaul_remove_plane(bool, OBScanPlane_&, int&)` `0x5c5c0`/`0xc3b18` | the plane-removal tool |

**Gates and codes (row #58 — the map, verified site by site).**

| setter (native) | gate | code |
| --- | --- | --- |
| `config_ir_exposure_time` `0x6f5a0c`, `config_color_exposure_time` `0x6f5a78`, `config_depth_range` `0x6f5b00`, `config_ir_gain` `0x6f5bb0` (not exported), `config_laser_current` `0x6f5c10`, `config_combined_*` `0x6f691c` | `[scan+0x3c] == 1` | **`0x32620110`** (`MULTI_CALLED`) — the same gate in all six |
| `config_show_data_rotate` `0x6f7dc8` | `[scan+0x3c] == 1` | **`0x32620105`** (`UNSUPPORTED_CONFIGURATION`) — a *different* code for the same flag |
| `config_light_type` — base `0x6f5c84` | (no override for the class) | **`0x32620105`** (a stub; the Laser override `0xc3fa4` does the work and returns `0x32620100` on its one error path) |
| `config_rgb_fill_light` `0x6fa630` | — (no `+0x3c` gate) | the device call's status; on success also `update_realtime_is_open_rgb_fill_light` (`0x701680`) |
| `reconfig_scan_mode_config` — Speckle `0x5cb00` | the `OrbbecScanStep` gates (`scan+0x4c8 == 3` + `+0x550 == 2`) | **`0x32620110`** — implemented as `w21 = 0x32620101; … add w21, w21, #0xf` (`0x5cb74`/`0x5cd84`); its other failure paths return **`0x32620101`** (`0x5d0b8`/`0x5d0e4`) |
| `reconfig_scan_mode_config` — Laser `0xc41a4` | an early gate (`0xc4254`; the sweep attributed it to the `+0xe1` latch `[inferred]`) | **`0x32620105`**; its device-push failures return **`0x32620100`** (`0xc433c`/`0xc43b8`/`0xc43d4`) |
| the device-manager callees (e.g. `set_ir_exposure_time` `0x5c6400`) | the device-present flag / callback checks | `0` or **`0x32620100`** |

So: the `+0x3c` gate means `0x32620110` for the exposure/depth/laser/combined
family and `0x32620105` for `show_data_rotate`; the mode reconfig's step gates
surface as `0x32620110` (Speckle) or `0x32620105` (Laser, early); the
"not supported by this class" stubs are `0x32620105`; and the device-level
failures are `0x32620100`/`0x32620101`. The `+0x3c` flag itself stays
unmapped (its writer is not in the setter paths) — **OQ-S13**. Treat any
non-zero as a refusal.

### Creation vs post-creation

`ObScanConfig` = `{ObScanModeConfig mode_config@0x00; const char* name@0x38}`
— the create-scan config *embeds* the mode config. The constructor-only path
(`OrbbecScanSpeckle/Laser::set_scan_config(OBScanConfig const*)`
`0x6bfbc`/`0xc8e50`; no static callers outside creation) stores the mode config,
pushes it to the device and sets the `+0xe1` latch. Post-creation the same
effect is reached by `reconfig_scan_mode_config`, which takes the bare
`ObScanModeConfig` the getter returns — a clean read-modify-write.

### The seam statement (docs/architecture/scan-viewer.md §6)

- **`setExposure(const Exposure&)` is directly implementable** — the dedicated
  setters exist and write through to the device live
  (`config_ir_exposure_time`, `config_color_exposure_time`,
  `config_laser_current`; `config_combined_exposure_and_laser_current` for the
  single-slider case; `config_rgb_fill_light` for the fill light).
- **`setFov(ObScanFov)` is implementable via read-modify-write** —
  `obscan_scan_get_mode_config` → set `scan_fov` (and any sibling fields) →
  `obscan_scan_reconfig_scan_mode_config`. There is no dedicated set_fov; the
  seam must preserve the other mode-config fields (re-read before writing if
  anything else may have changed them). Post-creation reconfig is legal — the
  `+0xe1` latch is already set by creation.
- Neither seam method needs a re-create path. The one caveat to document: the
  mode reconfig stops and re-starts the scan pipeline (a visible
  interruption), so FOV changes are heavier than exposure changes.

## 19. The voxel sizes — per-mode, per-session (row #64)

*Static half + on-disk evidence, plan64 pass. The corpus
(`pipeline-quality-assessment.md` §3 item 1: "align voxel sizes with what the
real app uses … the real app's 0.5 and 0.25") was re-derived; it does **not**
survive as a misalignment claim.*

**The contract's voxel fields.** `ObScanModeConfig` carries two:
`double voxel_size@0x20` and `double detail_voxel_size@0x30`. They are set at
creation (`ObScanConfig.mode_config`) and changeable post-creation only through
the whole-config `obscan_scan_reconfig_scan_mode_config` (§18 — there is no
dedicated voxel setter). A **third, separate surface** exists: the
meshlization/optimization config's own `voxel_size` — the app's
`scanDataConfig.dat` shows `OBScanOptimizationConfig {compute_speed,
voxel_size, credible_ratio, remove_markers, export_format}` per optimization
run (0.1, 0.1, 0.2269 in that project), and the C exports
`obscan_session_get_default_meshlization_config` (`0xcb8c`) /
`obscan_session_get_applyed_meshlization_config` (`0xcc04`) expose it
(undeclared in our header — a follow-up).

**The app's actual values are per-mode and per-session.** The app's
`scanParamConfig.dat` (JSON) carries the parameter sets side by side; the same
laser field differs across two real sessions:

| file (session) | parameter set | `VoxelSizeProperty` | `DetailVoxelSizeProperty` |
| --- | --- | --- | --- |
| the operator's `example project from CScan` | `LaserScanParam` (projection 1) | **0.1** | **0.05** |
| the captured `creality_scan_comparison*` sessions | `LaserScanParam` (projection 1) | **0.5** | **0.25** |
| the operator's project | `InfaredScanParam` (projection 0) | 0.5 | 0.25 |

with the app's own ranges `VoxelSizeRangeProperty [0.1, 2.0]` and
`DetailVoxelSizeRangeProperty [0.05, 0.25]` (the operator's laser set reads a
degenerate detail range `[0.05, 0.05]`). So the value is a **per-session
property**, not a fixed reference — the corpus's 0.5/0.25 is one session's
setting, not "what the app uses".

**Our tools.** `tools/scan_server/main.cpp:689-690` and
`tools/live_capture_test/main.cpp:458-459` both pass `voxel_size = 0.1`,
`detail_voxel_size = 0.05` — **exactly the operator's laser-session values**,
and both sit at the minimum of the app's ranges (the finest allowed).

**Verdict: nothing to change.** The 125x-voxel-count arithmetic is right
((0.5/0.1)^3 = 125; (0.25/0.05)^3 = 125) but its premise — that our values are
out of line with the app's — does not survive re-derivation: the app's values
vary per session, and ours match a real one. If a *resource* goal wants a
coarser grid, the right move is to expose the voxel size as a config knob
bounded by the app's ranges (voxel [0.1, 2.0], detail [0.05, 0.25]) — a
deliberate choice, not an "alignment". No build change is warranted by this
row; no header change (the mode config's fields are already declared; the
meshlization config is a separate follow-up).

**Cross-notes from the same files:** `PlaneRemoveProperty` is `true` in the
IR set and `false` in the laser one (relevant to G1's adoption); the device
block confirms the Raptor Pro / fw 1.5.7 / sn `CR0886100SN`.

## OPEN QUESTIONS

- **OQ-S1** — *Answered* (row #24) → §5. `scan+0x4c8` is the `OrbbecScanStep`
  (1 ready, 2 scanning, 3 paused, 4 stopped, 5/6 post-process, 9 mesh-adjacent,
  10 meshlization, 16 texturization); the color getter is legal for 1..9, the
  mesh getter for == 3.
- **OQ-S2** — Does anything publish temp→show on pause/stop (i.e., would
  `get_raw_cloud` return data after pause *without* the color getter)? `pause`
  clears `+0x4fa` in one branch; no publisher call found in its prologue — not
  exhaustively traced.
- **OQ-S3** — *Retired (row #67)*: the `+0x2e0` vs `+0x320` asymmetry is a
  vendor race, not a coarse lock — the model-cloud slot's dedicated lock
  (`+0x2e0`, used coherently by the internal family: `get_model_cloud`,
  `get_model_cloud_ptr`, both writers) is not the one the C-facing
  `get_raw_cloud` takes (`+0x320`), and no writer's caller holds the coarse
  lock. §2 carries the sites and the watch signature; our copy-out rule is the
  mitigation.
- **OQ-S4** — The marker-framework `+0x4fa` users (`judge_need_swap_fixed_model`,
  `get_realtime_fusion_model`) — trace when the marker path is specced.
- **OQ-S5** — *Actionable, hardware:* does polling the color getter
  (`obscan_scan_get_model_point_color_clouds`) once per tick make `get_raw_cloud`
  (or the color call itself) return live points during an active scan? Do not
  assume until a run shows it — this is the direct test of §3's mechanism.
- **OQ-S6** — *Actionable, hardware:* the stop tail does not fit the watchdog
  budget at scale (§16). The model save completes (both runs' `model.ply` are
  complete, 28 B/vertex); the out-of-core `.obmc` serialization was still
  running when the 420 s watchdog (`session_seconds + 120`, measured from
  `fork()`) fired — its last write is the same second the process exited, i.e.
  cut off mid-tail, not a post-save stall. Dynamic trace: reproduce the scale
  run and time the phases (live loop / pause → mesh → broadcast / stop
  preamble / tail) against the 420 s budget; if the tail still overruns,
  `sample <worker-pid> 30` (or `lldb -p <pid> -o 'thread backtrace all'`)
  during it names the function (expect
  `OutOfCoreSerializableLaserOnlineFusionOpt::to_buffer`); plus `lsof -p`
  (which file it holds) and a size poll of the `.obmc` (a steady growth rate
  is the throughput the budget must cover). A controlled A/B (a wider margin
  vs a smaller model) shows what the tail needs.
- **OQ-S7** — *Narrowed (row #44)*: the pose is the scanner's pose in the
  world, `p_world = M · p_scanner`, applied as-is (§17.1, verified math +
  storage shim). What remains is the optimizer's own sign convention
  (`OrbbecScanCombination::optimization` `0x56570`) and the world up-axis/units
  — settle with one `get_global_pose` at a known device pose (cross-check the
  frame pose).
- **OQ-S8** — *Answered (row #44)*: the three arrays are position
  (`{x,y,z,pad}`), normals (same shape) and radius (`f32`) per marker,
  anchored by the vendor's framework schema string (`0x6d594c`) and the
  serializers (§17.3). Residual: the VKeyPoint fields *not* exposed by
  `ObScanMarkers` (`+0x0c..+0x20` — ellipse coordinates/ints) stay unmapped;
  only needed if a consumer wants the raw ellipse data.
- **OQ-S9** — *Answered (row #44)*: the getter is repeatable, and an update
  frees a retained view (the whole-container `shared_ptr` swap under the lock;
  §17.3). Hardware-only residue: whether an update actually interleaves with
  an app read in practice, and the live cadence — a double-call loop during an
  active scan (guard page/ASan) would show it.
- **OQ-S10** — *Answered (the analyzer pass), with residuals*: the pinned C ABI
  has no intrinsics getter, but the session's project cache is a SQLite DB
  whose `files` row `camera_intrin.bat` holds the packed `DeviceParameters`
  record (written at preview/new-scan start; verified in a real session dir,
  210 B) — readable with any SQLite client, schema positional/big-endian,
  41 fields with the colour-intrinsics block anchored (§17.4). The
  backup/resume blob is produced in libOrbbecSDK (out of snapshot) and stays
  an unproven candidate. Residuals: the byte-exact map of all 41 packed fields
  (the identity/version fields are not yet named) and the blob's content
  (needs the libOrbbecSDK binary or a runtime hexdump).
- **OQ-S11** — *Answered (row #44), one gap*: `obscan_get_marker_mesh`
  (`0x11ce4`) is `int (int n, float* out_a, float* out_b, <float/vector args>)`
  — an n-segment disc/fan (`2πi/n` + `sincosf`) into the two caller buffers
  (`out_a` n×36 B = 9 `f32` triangles, `out_b` n×72 B = 18 `f32`), returns 0,
  frees its internal scratch; `n < 0` throws `std::length_error` (not an
  ErrCode). Gap: the float/vector argument block's lane roles are not
  statically derivable — an entry-register capture + a small-`n` hexdump
  settles it; its caller is unproven (absent from the app's P/Invoke
  tables). See §17.6.
- **OQ-S12** — *Resolved (row #60)*: the trigger **is** a C-reachable export —
  `obscan_registration_confirm_or_cancel_result(handle, bool)` — and it heads a
  complete exported registration family (§17.2). The pose-path trio
  (`session_create_registration`, the trigger, `registration_destroy`) is now
  declared in the header; the runners' signatures remain a follow-up if the
  viewer must *run* registrations.
- **OQ-S13** — *Answered (row #58), two residues*: the gate→code map is in §18
  (the `+0x3c` gate → `0x32620110` for the exposure/depth/laser/combined
  family and `0x32620105` for `show_data_rotate`; the reconfig's step gates →
  `0x32620110` (Speckle) / `0x32620105` (Laser, early); the base stubs →
  `0x32620105`; the device failures → `0x32620100`/`0x32620101`). Residues:
  (a) who writes the `+0x3c` flag and what it means (it is not written in any
  setter path; a `[scan+0x3c] == 1` state that refuses all six setters —
  likely "scanning/stopping", `[inferred]`); (b) the exact gate at the Laser
  reconfig's `0xc4254` (`0x32620105`) — the `+0xe1` attribution is the sweep's
  `[inferred]`. Both are listing-answerable with a focused pass; neither
  blocks the C-facing rule (non-zero = refusal).
- **OQ-S14** — *Answered (row #58)*: the C wrapper (`0xf8cc`) takes
  `(scan, const ObScanManualRPConfig_*)` — one config pointer — and logs
  `obscan_scan_config_manaul_remove_plane(%s)` from the config's `+0x4` byte;
  NULL scan → `0x32620107`; else it tail-calls vtable `+0x58` with
  `(scan, config->enabled@0x4, &config->plane@0x8, config)` — the native's
  `(bool, OBScanPlane_&, int&)` (`0x5c5c0`/`0xc3b18`), whose third argument is
  the config pointer itself (read as the int at `+0x00`, compared `== 2`).
  So the config's verified prefix is
  `{ int32_t @0x00; bool enabled @0x04; pad; OBScanPlane_ plane @0x08 }`.
  Residual: `OBScanPlane_`'s own layout (not recovered here) — the header
  declaration waits on it.

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

Row #44 (the pose/marker residuals): four read-only sweeps over the existing
snapshot (S7 the pose convention, S8 the marker field names, S9 the borrow
stability, S11 the marker-mesh signature) — no analyzer run. Targets/addresses as
cited in §17.1/§17.3/§17.6 and the OQ list; the sweep drafts were folded and
deleted.

OQ-S10 (the camera intrinsics, the analyzer pass): the write chain was traced in
the listing (save_camera_intrins -> SerializableDeviceParams::to_buffer ->
ProjectFileCache::store_serializable) and the record verified in a real session
cache (`<output_dir>/resources.obscan`, SQLite, `files.name = camera_intrin.bat`,
210 B) — plus the packer format (0x1a03f4) and the 41-field order (0x6e3874). The
backup/resume blob's producer is a libOrbbecSDK virtual (out of snapshot).

Row #55 (the config apply surface): the export table (the `_obscan_scan_config_*`
family), the wrapper region 0xedcc-0xf2f0, the implementation names for the
value types, and the natives sweep (row55-a draft, folded); `ObScanConfig` vs
`ObScanModeConfig` from the header + `set_scan_config` 0x6bfbc/0xc8e50.

Row #58 (the config-setter residues): light reads only — the setter natives
(0x6f5a0c-0x6f5d20), the reconfig pair (0x5cb00/0xc41a4), the device callee
(0x5c6400), and the plane wrapper/native (0xf8cc/0x5c5c0). No analyzer.

Row #60 (the pose trigger): the registration family wrappers (0xda1c/0xde2c/0xdecc),
the trigger native (0x55334), and the family listing from the dynsym. Light reads
only; no analyzer.

Row #64 (the voxel sizes): the app-side primary evidence re-read — the
operator\u2019s CScan project files `scanParamConfig.dat` / `scanDataConfig.dat`
(JSON) plus the captured copies under the verify worktree\u2019s
`logs/creality_scan_comparison*`. Light reads; no analyzer.

Row #67 (OQ-S3): both lock sites read statically — the model-cloud family
(0x7038e4/0x703930/0x703828/0x703894, all +0x2e0) vs the C-facing reader
(0x702f4c, +0x320), plus the 13 update_model_cloud callers' lock state. Light
reads; no analyzer.
