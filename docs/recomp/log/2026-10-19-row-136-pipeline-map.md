- 2026-10-19 — build-3 (build room, `wt-build3`, branch `build/plan136-pipeline-map`): **#136 — where
  the scanner's data goes.** A stage-by-stage map of the live pipeline, with the volume at each stage,
  the code path, and a classification. **No code was changed** — this is an audit of code reads plus
  the artifacts of a real run. Verdict on the operator's hypothesis
  ("I don't think we are even writing all the data anywhere yet"): **confirmed for our pipeline,
  refuted for the product** — the vendor SDK does write the full cloud, to a path we choose but
  never read back, while everything *we* carry is decimated, positions-only and ephemeral.

  Method: code reads at the cited lines; volumes computed from the #112 baseline run
  (`tests/results/2026-10-19-plan112-tick-latency-baseline.raw.log`, 2,349 cloud frames, a final
  published cloud of 1,366,566 points and a 6,323,235-vertex mesh); and the SDK's own output
  artifacts, which still exist from a `scan_server` run under
  `/tmp/openscanner_live_server_session/` and are cited with their sizes below. `[inferred]` marks
  anything that is inference rather than a read or a measured artifact.

  ## The map

  | # | Stage | What data | Volume per frame / second | Code path (file:line) | Kept / dropped / decimated | Classification |
  | --- | --- | --- | --- | --- | --- | --- |
  | 0 | **Scanner → vendor fusion** | depth/IR frames fused into a model cloud at the configured voxel size | continuous; the cloud grew to 1,366,566 points by the run's end | `tools/scan_server/main.cpp:941-954` (`scan_config`: `post_texture = OB_SCAN_NO_TEXTURE` `:942`, `voxel_size = 0.1` `:953`, `detail_voxel_size = 0.05` `:952`) | the SDK fuses at a 0.1 voxel and produces **no texture** (no color) | **by design** (and the reason there is no color anywhere below) |
  | 1 | **Vendor API exposes** | `get_model_point_color_clouds`: points + normals + **colors** + bbox; `get_raw_cloud`: points + normals; `get_realtime_model_mesh`: vertices + normals + **triangles** | full cloud 1,366,566 pts ≈ 16.4 MB as xyz alone, 38 MB with normals+colors; mesh 6,323,235 verts / 9,927,768 tris ≈ 76 MB verts + 119 MB tris | `engine/include/openscanner/obscan_api.h:227-250` (clouds), `:266-270` (mesh); loader resolves them at `engine/src/obscan_loader.cpp:115-137` | **we read the point array only** — `normals` and `colors` are never touched; `get_raw_cloud` is **resolved but never called** by `scan_server` (only `tools/live_capture_test/main.cpp:559,592`); mesh `normals`/`triangles` are read as counts, never copied | **unbuilt** (color, normals, triangles) / **unused** (raw cloud) |
  | 2 | **`scan_server` per tick — read** | `get_model_point_color_clouds` into a borrowed `cloud` | one call per tick, ~8.7/s; source cloud up to 1,366,566 pts | `main.cpp:1174`; release at `main.cpp:1272` (vendor no-op) | the read itself keeps everything in the SDK's buffer; nothing is copied yet | **by design** |
  | 3 | **`scan_server` per tick — decimate** | stride-sampled `xyz` only (no normals, no colors) | **20,000 pts** out of 1,366,566 → **98.5 % dropped per frame**; 174,000 pts/s at 8.7/s; 2.09 MB/s | `main.cpp:1183` (`kLiveBroadcastPointBudget = 20000`), stride at `:1225-1228`, xyz build at `:1229-1243` | **decimated 20000:1** and **positions-only** | **by design** for the live UI (bounded frame, keeps the page live); it is *the* data-loss stage for anything downstream |
  | 4 | **V1 frame + broadcast** | `cloud_snapshot`: 36 B envelope + 20,000 × 12 B | **240,036 B/frame** (234 KiB), 2.09 MB/s | `main.cpp:1243` `BuildFrame` → `:499-503` → `engine/include/openscanner/frame_protocol.h:36-73` (`encode_cloud_snapshot`, comment `:38` "positions only"); broadcast `main.cpp:1247` | only positions — the protocol has no field for color or normals | **by design** (V1 as specified) |
  | 5 | **`RecordFrame`** | the **same decimated, positions-only** buffer | 240,036 B/frame; cap **1 GiB** = 4,473 frames ≈ **8.6 min** at 8.7/s | `main.cpp:505-541`, cap at `:510`; called `:1251`, `:1266` | **records the broadcast, not the cloud** — and it is a **no-op without `--record`** (`:506`) | **by design** for a wire capture; **not a project save** |
  | 6 | **Teardown snapshot** | `get_realtime_model_mesh` → 20,000-pt preview + full-detail chunks **vertices only**; the mesh is released without triangles being copied | preview 240,036 B; full detail 317 chunks × 240,036 B ≈ **76.1 MB** | `main.cpp:1303` (getter), preview `:1310-1327`, chunks `:1336-1352`, release `:1356` | **all vertices kept, all triangles dropped** — the page gets a point soup, not a mesh | **by design** for the viewer; **unbuilt** as a mesh export |
  | 7 | **Browser** | accumulates the full-detail vertices in a `Float32Array` and renders them | 6,323,235 × 12 B ≈ **75.9 MB in page memory** | `ui/app.js:253-262` (`full_mesh_start` → `fullMeshAccum = new Float32Array(...)` `:254` → `renderer.upload` `:261`) | **nothing persisted**: no download, no Blob, no `localStorage`/IndexedDB in `ui/app.js` | **unbuilt** (no export) |
  | 8 | **Project folder (the SDK writes it)** | `model.ply` (x,y,z,**intensity**,nx,ny,nz — 28 B/vertex) + `mainLOMCache.obmc` (out-of-core fusion) + `resources.obproj` | real artifacts on this machine: **`model.ply` 960,974 vertices / 26,907,494 B**; **`mainLOMCache.obmc` 657,436,206 B**; `resources.obproj` 24,576 B | `main.cpp:930` `session_config.output_dir = "/tmp/openscanner_live_server_session"`; the write is the vendor's, inside `obscan_scan_stop` (findings `scan-live-data.md` §16 items 3-4) | **the full cloud IS persisted** — by the vendor, not by us; in `/tmp`; **no color** (NO_TEXTURE mode) | **by design (vendor's) but unbuilt on our side** — nothing reads it back and nothing copies it out of `/tmp`; the only code references are the assignment at `main.cpp:930` and the header's own note at `engine/include/openscanner/obscan_api.h:679` |
  | 9 | **Plane removal / throttling** | `OPENSCANNER_PLANE_REMOVE=1` / `_TURNTABLE_` / `_MANUAL_PLANE`: the **vendor's** own per-frame removal — changes the cloud every stage above reads; `OPENSCANNER_OUR_PLANE_REMOVE=1`: our own fit-and-cut, applied **after** the read and **before** decimation, so it drops points from the **broadcast only** | our path drops the plane's face from the 20,000-pt sample (counters `g_our_plane_cut`/`kept`) | vendor toggles `main.cpp:987-1019`; ours `main.cpp:1190-1213`, cut at `:1233-1240` | affects the broadcast only (ours) / the whole cloud (the vendor's, only when enabled) | **by design**, opt-in, and **off by default** |
  | 10 | **Throttle (time, not data)** | — | tick floor 100 ms (`kLiveTickIntervalMs`) | `main.cpp:652`, sleep `:1097`/`:1099` | sets the *rate* at which stages 2-5 run (~8.7/s against a 10 Hz floor) | **by design** (row #113 owns the cadence) |

  Volumes in the table are per frame unless stated. The V1 envelope is 36 B (magic 4 + version 2 +
  type 2 + seq 4 + t_capture 8 + t_server 8 + payload_len 4 + count 4), then 12 B/point; verified
  against `engine/include/openscanner/frame_protocol.h:36-73`.

  ## The verdict, stage by stage

  **Confirmed: our pipeline never carries the whole scan.** Three independent losses, all by design
  but none of them a *save*:

  1. **Decimation at the broadcast, 98.5 %.** `kLiveBroadcastPointBudget = 20000` (`main.cpp:1183`)
     against a 1,366,566-point cloud: the live stream carries 1.5 % of the cloud, per frame, every
     frame. The full cloud exists only in the SDK's buffer, and the tick releases it
     (`main.cpp:1272`) without ever copying it.
  2. **Positions only, twice.** The V1 `cloud_snapshot` has no color field
     (`frame_protocol.h:38`), and the tick builds `xyz` from `cloud.points` alone — the getter's
     `normals` and `colors` arrays are never read. The mesh path likewise copies `vertices` and drops
     all 9,927,768 triangles (`main.cpp:1340-1352`). Independently, the scan is configured
     `post_texture = OB_SCAN_NO_TEXTURE` (`main.cpp:942`), so the vendor produces no color in the
     first place.
  3. **The browser is memory-only.** The full-detail vertices (75.9 MB) live in a `Float32Array`
     (`ui/app.js:254`) and are gone when the page closes; `ui/app.js` contains no download, Blob,
     `localStorage` or IndexedDB call.

  **Refuted: the data is not lost from the product.** The vendor SDK writes the **full accumulated
  cloud** during `obscan_scan_stop`, into the `output_dir` we set (`main.cpp:930`), and the
  artifacts are on this machine from a real `scan_server` run:
  `/tmp/openscanner_live_server_session/sTxVn6N_a6zuqC_PjwNEl_XBUgzM/result/model.ply` — 960,974
  vertices, binary little-endian, `property float x,y,z,intensity,nx,ny,nz`, 26,907,494 B — plus
  `temp/mainLOMCache.obmc` at 657,436,206 B (the out-of-core fusion cache) and a sibling project dir
  with `resources.obproj`. Per `docs/recon/findings/scan-live-data.md` §16, that write is the
  vendor's `write_cloud_with_intensity_ply(..., <outdir>/result/model.ply, /*binary=*/1, ...)`, and it
  is gated on `output_dir` being non-empty — which is the only thing our code does about it.

  So the honest answer to "are we writing all the data anywhere yet?" is: **the SDK is, and we are
  not.** The scan reaches disk, in a form we never read, at a path in `/tmp` that the code touches only to set
  (`main.cpp:930`) and the header notes (`obscan_api.h:679`), and that macOS may clean up.

  ## The one gap that matters most

  **The project artifact is written where nobody can find it, and nothing reads it back.** Everything
  the product *displays* is decimated and positions-only; everything the product *keeps* is a vendor
  binary PLY plus a 657 MB opaque `.obmc` in `/tmp`, with no copy-out, no listing, no reader, no
  colour, and no integration with the app's UI. That is a product gap, not a data-loss bug: the data
  is captured and written, it is simply not *owned* by us. Closing it needs (a) `output_dir` moved to
  a real project location the app chooses and can show, (b) a reader for `model.ply` (a binary PLY
  with intensity and normals is straightforward) and a decision on the `.obmc` (vendor-internal, and
  §16 shows it is the piece the watchdog cuts), and (c) a decision about colour: either accept
  positions+intensity-only as the product's data model, or enable texture and carry colour through
  stages 3-6.

  ## Where the time goes, on the same map

  From #134's audit: the tick is **sleep-bound** at a 100 ms floor (`kLiveTickIntervalMs`,
  `main.cpp:652`), with **~1.23 ms timed** across the four segments (`pose` 0.00, `getter` 0.00,
  `post` 1.68, `bcast` 0.11 mean in the #112 run's outlier window; `main.cpp:1144`/`:1174`/`:1243`/
  `:1247`) and **~9.6 ms untimed** — the per-tick `obscan_scan_config_combined_exposure_and_laser_current`
  re-assert (`main.cpp:1130`) and `obscan_scan_model_clouds_release` (`main.cpp:1272`), plus the
  50-tick `printf` (`main.cpp:1285`) and `RecordFrame`. So the completeness gaps are **not** a
  speed problem: reading the full cloud costs the same 0.00 ms (`getter`); the decimation is a
  *choice* made at `:1183`, not a time constraint. Decoupling the fetch from the broadcast (#113's
  producer/consumer idea) would let the tick carry the full cloud without touching the 100 ms floor.

  ## What this changes for the board

  Nothing here contradicts #112/#115 — the pipeline is fast; it is *lossy by design* and its one
  durable output is vendor-owned and unread. The narrowed questions this audit leaves:

  - **#136a** (unbuilt): move `output_dir` off `/tmp` to a project location the app surfaces, and add
    a reader/listing for `model.ply` (+ a decision on `.obmc`). Wall: `tools/scan_server`,
    `tools/macos_app`, `ui/`.
  - **#136b** (decision): colour — accept "positions + intensity, no RGB" as the data model (then
    delete the `colors` expectation from the map), or enable texture in `scan_config` and carry it
    through the broadcast/record. Wall: header decision (recon) + `tools/` + `ui/`.
  - **#136c** (by design, not a bug): the 20,000-point live budget stays — the UI needs a bounded
    frame. If the *record* is meant to be the archive, it should record a different stream than the
    broadcast (full cloud, or the vendor PLY path), not the decimated one.

  ## Caveats

  - One run's numbers (the #112 baseline) for the volumes, plus artifacts from a later run on this
    machine; the *shape* of the map is code-read and does not depend on a particular run.
  - `[inferred]`: that macOS's `/tmp` cleanup will eventually remove the project artifacts — no run
    has demonstrated the loss. The *absence* of any reader or copy-out is read, not inferred.
  - The `.obmc` is vendor-internal (out-of-core fusion); §16 records the watchdog cutting it
    mid-tail in a long run, so it is not a reliable archive even when present.
