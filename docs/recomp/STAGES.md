# The stage map — every stage, with time in it

**Row #155.** The companion to `docs/recomp/SCALE-ARCHITECTURE.md` (the *structure* argument) and to
`docs/recomp/log/2026-10-19-row-136-pipeline-map.md` (the *volumes* map). #136 says what data exists at each stage and
what is dropped; **this file adds the time** — the measured cost of each stage, its budget, and its contribution to
the frame's age — and it follows the data all the way to the pixels, then names the **future logic stages** as
placeholders with the inputs and budgets they will need.

**Discipline.** Every cell either cites its source or says **"not measured"**. No cell carries a guess, an
extrapolation dressed as a measurement, or a number from a different pipeline. Where a number is *derived*, the
derivation is shown. The line numbers are as of `origin/main` = **`4adc533`**; **the anchor (the function or the
statement) is the citation** and the line number is a convenience that will drift.

**Sources.** `#136` = `docs/recomp/log/2026-10-19-row-136-pipeline-map.md` (volumes, drops, code paths);
`#147` = `docs/recomp/log/2026-10-19-row-147-runtime-baseline.md` (the device-free profile: CPU samples, the syscall
view, the allocation footprint, the platform's wait floor); `#141` = the tick instrument
(`tools/scan_server/perf_window.h`: the seven segments, the overshoot, the residual); `#112` = the hardware baseline
(`tests/results/2026-10-19-plan112-tick-latency-baseline.md` and its HUD capture); `#151` = row #151's rate finding;
`[SPEC]` = `docs/recomp/SPEC-TARGETS.md`.

---

## 1. The chain

```
  device capture ──► USB / GVCP ──► vendor fusion ──► SDK getter ──► our ingest
   (vendor)          (vendor)        (vendor)          (0.00 ms)        (0.00 ms)
                                                                       │
                                                                       ▼
   ┌──────────────────────────────────────────────────────────────────────────┐
   │ the tick's critical path:  decimate ──► encode ──► broadcast ──► pace    │
   │                            0.68 ms*     (in the 0.68)   37 µs/client       │
   │  *decimate+encode measured together; the split is not measured           │
   └──────────────────────────────────────────────────────────────────────────┘
                                   │  27,697 B per client per frame
                                   ▼
                        browser parse ──► accumulate ──► render
                        (not measured)   240 kB live /   59.5-60.1 fps
                                       75.9 MB at teardown
                                                                       │
                                                                       ▼
                     the future logic stages: tracking · marker ID ·
                     ellipse transforms · exposure  (placeholders, §4)
```

---

## 2. The stages

| # | stage | ours / vendor | data type + size | code location (anchor) | measured cost | budget (the preset) | latency contribution | #136 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **device capture** | vendor | depth/IR frames; the profile requests `[streamtype, 9, 1920, 1200, 50]` — 50 fps *nominal*; the **fused cloud arrives at 24.0 Hz** | the device; [SPEC], `protocol-spec.md` §7 | **not measured** | not ours | the device's capture→publish leg: **not measured** — the model-cloud path exposes no capture time (the ABI's timestamp getters are on the *frame*/*image* paths, `obscan_api.h:924`/`:950`) | — |
| 2 | **USB / GVCP** | vendor | the transport: GVCP discovery on UDP 3956, msgpack-RPC over TCP (10010 depth / 10013 IR); the vendor's client polled `ob_rpc_get_frame` at **85–86 Hz** | `protocol-spec.md` §1.1/§5; `tools/raptor_client/main.cpp` (`GvcpDiscover`) | **not measured** | not ours | **not measured** — and note this is the *raptor* path; the OBScan path's transport is the SDK's own USB path | — |
| 3 | **vendor fusion** | vendor | the model cloud at a **0.1 voxel**: **1,366,566 points** by the run's end (16.4 MB as xyz, 38 MB with normals+colours); `NO_TEXTURE` → **no colour at all** | `scan_config` — `post_texture` (`main.cpp:937`), `voxel_size` (`:948`) | **not measured** (the SDK's own threads) | not ours | the vendor's publish latency: **not measured** | 0 |
| 4 | **the vendor's own persistence** | vendor | `model.ply` 960,974 vertices / **26.9 MB** + `mainLOMCache.obmc` **657 MB**, written at `obscan_scan_stop` into the `output_dir` we set | `session_config.output_dir` (`main.cpp:925`) | **not measured**; **657 MB of disk per scan** | the disk budget (#149's "bounded by disk") | off the live path — it happens at the teardown | 8 |
| 5 | **the SDK getter** (our call) | vendor, called by us | the **borrowed** cloud: points + normals + **colours** + bbox — 16.4 MB as xyz at the run's end | `obscan_scan_get_model_point_color_clouds` (`main.cpp:1212`) | **0.00 ms** (mean; max 0.18 ms) — #112's `getter` segment. *Our call* is free; the fusion above is the SDK's work on its own threads | not ours | **the ingest instant** — this is where `t_capture` should be taken (the note's C1) | 1 |
| 6 | **our ingest**: read + release | ours | the borrowed pointer — **no copy**; the cloud is valid only until the release | the getter above → `obscan_scan_model_clouds_release` (`main.cpp:1315`) | the read **0.00 ms**; the release **not measured** (#141's `release` segment; row #141 is `HW`) | **0 allocations per tick** (the ingest copies nothing today) | the tick's read instant | 2 |
| 7 | **decimate** | ours | the **stride** over the growing cloud → **20,000 points** (**98.5 % dropped** per frame); 240,000 B of `xyz` | the stride (`main.cpp:1264-1267`) and the build (`:1268-1280`) | **not separately measured** — the live `post` segment measures decimate+**encode** together at **37.5 ns/point** (the slope between the two end points of #112's per-window p50s: (0.68 − 0.04) ms over (17,943 − 862) points); the split between them is not measured (see the note's §4.1 disagreement) | the tier's **point budget**; the stride is a *number*, not a branch | in the tick's critical path | 3 |
| 8 | **encode** | ours | the V1 envelope: 36 B + 4 B + **12 B/point** = **240,036 B** at 20,000 points | `BuildFrame` (`main.cpp:1282`) → `encode_cloud_snapshot` (`frame_protocol.h:39-73`) | the encoder alone: **53.4 ns/point**, 63 % of the tick's CPU (#147's device-free profile); with the decimate: **0.68 ms p50** at 17,943 points, **1.18 ms mean / 5.05 ms max** over 2,522 ticks (#112) | **0 per-byte writes**; one sized write of 240,036 B (the note's W1) | in the tick's critical path | 4 |
| 9 | **broadcast** | ours | the WebSocket frame: a 2–10 B header + **27,697 B**, **per client** | `BroadcastBinary` (`main.cpp:1286`) → `SendFrame`/`SendAll` (`ws_broadcast_server.cpp:255-256`, `:503-517`) | **37 µs per client per tick** (~748 MB/s); 296 µs for 8 clients; `__sendto` = **57.7 %** of the tick's CPU at 8 clients; **2 syscalls per client per frame** | **1 write per client per frame** (W4); the tier's bytes per client | in the tick's critical path; a **stalled client holds the tick for up to 100 ms**, sequentially under `clients_mutex` | 4 |
| 10 | **browser parse** | ours (the UI) | the decode → a `Float32Array` of `count × 3` (240 kB at 20,000 points) | `FrameProtocol.decodeCloudSnapshot` (`ui/app.js:288`) | **not measured** — there is no browser-side profile in our datasets | the tier's points | the page's first sight of the data | 7 |
| 11 | **accumulate** | ours (the UI) | the live path holds **one** 20,000-point cloud (240 kB, *replaced*, not accumulated — `ui/app.js:305`); the teardown's full-detail stream accumulates `msg.total × 3` floats = **75.9 MB** at 6,323,235 vertices (`ui/app.js:259`) | the two sites above | the page's **memory**: 75.9 MB — and it **grows with the scan**, so this is the one **unbounded** thing in the product | the tier's **accumulation cap** (#154's preset axis); the full-detail stream is a tier item | the teardown's accumulation is a **one-shot**, not a per-frame cost | 6, 7 |
| 12 | **render** | ours (the UI) | the GPU upload + the vsync loop | `renderer.upload` (`ui/app.js:305`); the HUD | **not measured**; the render loop's rate is **59.5–60.1 fps** (the #112 HUD capture) | the render loop's rate | the page **drops a live frame** when the renderer is behind (`ui/app.js:298`) — the *consumer-side* drop, which is not the server's ladder | 7 |
| 13 | **record** | ours | the same decimated buffer; the cap is **1 GiB** = 4,473 frames ≈ **8.6 min** at 8.7/s; a no-op without `--record` | `RecordFrame` (`main.cpp`) | **not measured** | the capture on/off axis (#149) | off the live path | 5 |
| 14 | **teardown snapshot** | ours | the mesh preview (20,000 points) + the full-detail chunks (317 × 240,036 B ≈ **76.1 MB**); all vertices kept, all 9.9 M triangles dropped | `main.cpp` (the teardown) | **not measured** | the tier's mesh-stream item (§4.2's ladder) | a one-shot at the end | 6 |
| 15 | **throttle / pace** | ours | a *time*, not a datum: the tick's period | `CFRunLoopRunInMode(kCFRunLoopDefaultMode, kLiveTickIntervalMs/1000.0, false)` (`main.cpp`, the live tick) | the wait is **99.4 %** of the tick (#141's instrument); the platform's floor for a 100 ms request is **103.19–103.75 ms** (#147 §5) → a **9.68 Hz** ceiling | the tier's **publish rate** (needs the note's C2 to be a knob) | the whole tick's period — this is the schedule | 10 |

---

## 3. The future logic stages (placeholders)

The operator's four consumers ([SPEC] and the note's §4.1). They are **not built**, so every cost cell is "not
measured"; what *is* fixed is the **input** each one needs, the **budget** it must fit in, and **where it runs**.

| stage | the input it needs | where the data comes from | the budget | where it runs | status |
| --- | --- | --- | --- | --- | --- |
| **tracking** | the scanner's **pose** over time — and the pipeline already carries a pose as its own message type (`MessageType::Pose`, `frame_protocol.h:20`; the tick broadcasts it every tick, `main.cpp:1192`). If it must track *surface features* instead, it needs a **stable** subset — and the stride decimation is **not** stable (the stride changes as the cloud grows) | the pose frame, or C1's slot at the consumer resolution | the **leftover frame budget**: **40.5 / 18.8 / 15.5 ms** at 24 / 50 / 60 Hz (the note's §3), minus the ingest's 0.75–1.2 ms | a **consumer thread** — never the ingest thread (the tick's pace is the pipeline's clock) | not built; the input is a first-class message today |
| **marker ID** | **identity across frames** — the same physical point in successive frames. The stride decimation cannot give it (the sampled set shifts as the cloud grows); the **vendor's 0.1-voxel grid** is a natural identity | C1's slot, at the **consumer resolution**: the full cloud (1.37 M points), or a coarser one | the *data* cost is the **copy** the consumer resolution implies — 240 kB/frame at 20k, 2.4 MB at 200k, 16.4 MB at 1.37 M (the note's §4.2) — plus its own compute, inside the leftover budget | a consumer thread, reading the slot | not built; the identity model (voxel key vs the raw cloud) is an open product decision |
| **ellipse transformations** | the region to transform, **at the resolution the transform is defined at** — the decimated stream changes the fit's *input*, not just its size | C1's slot, at the consumer resolution | as above | a consumer thread | not built |
| **exposure settings** | not a cloud consumer: a **device-parameter** consumer — the current exposure value and the ability to change it | the tick's `exposure` segment; the push is now **conditional** (row #153: `ApplyExposureIfChanged`, `main.cpp:1158`) | **0 pushes per tick** in the steady state; a push per *change* | it must **act on** the tick: a bounded "requested value" slot — #153's shape — and never a run inside the ingest thread | the device side is built (#153); the *logic* is not |

---

## 4. What each stage's cost is, in one place

| stage | the cost that is measured | the number | what it is measured by |
| --- | --- | --- | --- |
| the SDK getter | the call into the vendor | **0.00 ms** | #112's `getter` segment |
| decimate + encode | the live `post` segment, together | **0.68 ms p50** (at 17,943 points), **1.18 ms mean / 5.05 ms max** | #112's summary line + the per-window p50s |
| the encode alone | the device-free attribution | **53.4 ns/point** (63 % of the tick's CPU) | #147's Time Profiler |
| the broadcast | per client, per tick | **37 µs**; **57.7 %** of the tick's CPU at 8 clients | #147 §6/§7 |
| the tick's own overhead | the whole tick, with no client | **195 µs** of CPU per tick (0.53 % of a 37.0 ms period) | #147 §3 |
| the wait | the platform's floor | **103.19–103.75 ms** for a 100 ms request | #147 §5 (a zero-work control) |
| the allocations | the live set | **flat**: 86–87 KB, +7-8 allocations over 30 s | #147 §3.2 (`vmmap`) |
| the browser | the render rate; the page's memory | **59.5–60.1 fps**; **75.9 MB** at the teardown | the #112 HUD capture |
| the device stages | the capture, the transport, the fusion | **not measured** | — |

---

## 5. The cells this file cannot fill yet, and which row fills them

| the missing number | the row that will produce it |
| --- | --- |
| the per-stage **budgets** (the times, against the reference profile) | **#152** — the contract; the *shape* of the budget and the exact invariants are in the note's §4.1 |
| the `exposure` and `release` segments on hardware | **#141** (status `HW`) |
| the decimate/encode split | the note's §4.1 disagreement: the live and the device-free per-point figures do not reconcile, and a measurement that separates them does not exist yet |
| the end-to-end frame age | **#142** has the arithmetic (`server_now_ms` + `ui/frame_age.js`); the number comes from the next hardware run |
| the end-to-end per-stage total | **#156** — the full-frame benchmark |
| the browser's parse and accumulate costs | no row yet — a browser-side profile is a gap the note names |

---

## 6. How this file relates to #136 and to the note

* **#136** is the **volumes** map: what data exists at each stage, what is dropped (98.5 % at the decimate), and the
  verdict *"confirmed for our pipeline, refuted for the product"*. Its stage numbers are carried in the last column
  above, so the two maps line up.
* **The note** (`docs/recomp/SCALE-ARCHITECTURE.md`) is the **structure** argument: the limits, the seams, the budgets'
  shape, the adaptivity design, and the waste list. **This file is the table the note's argument rests on** — every
  number here is cited there, and every "not measured" here is a place the note's analysis had to reason without one.
* **Nothing in this file is a plan.** It is a map with time in it, and the honest marks are where the time is not
  known yet.
