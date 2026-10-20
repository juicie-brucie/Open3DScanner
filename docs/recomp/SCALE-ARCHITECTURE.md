# Scale architecture — the pipeline's structural limits, and the seams worth cutting first

**Row #148.** A proposal, not code. **No source file is changed by this row**; the only other file it touches is
`docs/recomp/PLAN.md` (the row's own status). Device-free: every number cited here was measured without a scanner
attached, or is read from a code location or from a recorded run.

**Why this file and not `docs/recomp/ARCHITECTURE.md`.** That file is the *recomp process* charter — the roles, the
walls, the models, the loop and the landing gate — and it is the document the clean-room posture rests on
(`ARCHITECTURE.md:224-228`). Overwriting it with a product-architecture proposal would destroy the charter, so this
note lives beside it and is linked from it. (`docs/recomp/ARCHITECTURE.md:107-123` is the charter's own index of
what lives where.)

---

## 0. The datasets, and how to read a number here

| tag | what it is | where |
| --- | --- | --- |
| **[#147]** | the device-free runtime baseline: CPU samples, the syscall view, the allocation footprint, the tick's CPU-vs-wall split, and a zero-work control of the platform's wait floor | `docs/recomp/log/2026-10-19-row-147-runtime-baseline.md` |
| **[#136]** | the pipeline map: 10 stages, the volume at each, the code path, what is kept or dropped, and the verdict *"confirmed for our pipeline, refuted for the product"* | `docs/recomp/log/2026-10-19-row-136-pipeline-map.md` |
| **[#141]** | the tick instrument: seven timed segments plus the overshoot and the residual | `tools/scan_server/perf_window.h:27-30`, `:99-100`, `:143-146` |
| **[#151]** | the board's rate finding: the source is **24.0 Hz**, our publish is **8.7 Hz** | `docs/recomp/PLAN.md`, row #151's note |
| **[SPEC]** | the vendor's advertised rates and what we have measured of them | `docs/recomp/SPEC-TARGETS.md` |

**Citation convention.** `[#147 §7]` is §7 of the baseline log; `[#136 stage 3]` is the stage-3 row of the map's
table; `[#141 perf_window.h:27-30]` is the instrument; `main.cpp:1295` is `tools/scan_server/main.cpp` line 1264 **on
`origin/main` = `5656a6b`**, the commit this note was written against; `[derived]` means arithmetic on cited numbers,
with the arithmetic shown inline.

**The lesson this note is written to.** Row #134's audit found that the "tick improvement" was an instrument
mismatch — two different instruments, compared as if they were one ([#134] `docs/recomp/log/2026-10-19-row-134-tick-audit.md`).
So every number below names its instrument, and a number whose instrument is "read the code" is labelled as such and
never mixed with a measured one. Where a live-path number is extrapolated from the device-free baseline, it says so.

---

## 1. The shape today, in one paragraph

One **single-threaded** tick polls the device every 100 ms, decimates the vendor's cloud to 20,000 points, encodes a
positions-only V1 frame, broadcasts it to every WebSocket client *inline*, records the same decimated buffer, and
releases the vendor's borrowed cloud — then sleeps for the next tick. The network is inside the tick's critical
path; the data is borrowed from the SDK; the product keeps almost nothing. `engine/` is already modular (loader,
worker, WS broadcast, mesh bridge); the two large files are `tools/scan_server/main.cpp` (**1,871 lines** on
`5656a6b`; the board row's "1,799" predates rows #139/#141, #142 and #153) and `tools/raptor_client/main.cpp`
(**2,526 lines**; the row's "1,992" predates row #140's capture work). The tick is **not** CPU-bound: at 0 clients the whole tick costs
**195 µs of CPU** — 0.53 % of its own 37.0 ms period ([#147 §3]) — and it is **not rate-starved either**: the
device's fused cloud arrives at **24.0 Hz** while we publish at **8.7 Hz** ([#151]), so there is a **2.8× publish
headroom** today, and it is our own 100 ms pace that spends it (§3).

---

## 2. The structural limits for scale

### L1 — The tick is one thread, and the network is inside its critical path

**Evidence.** The broadcast is synchronous in the tick thread: `BroadcastBinary` takes `clients_mutex` and sends to
every client inline (`engine/src/ws_broadcast_server.cpp:503-517`); `SendFrame` writes the header and then the
payload as **two syscalls per client per frame** (`:255-256`); each client socket has a **100 ms
`SO_SNDTIMEO`** (`:302`, `:307`). The measured cost: `bcast` p50 **0.00 / 0.10 / 0.29–0.31 ms** at 0 / 1 / 8
clients, and `__sendto` is **57.7 % of the tick's CPU** at 8 clients ([#147 §3, §6]). The tick calls it inline at
`main.cpp:1299` (live) and `main.cpp:620` (synthetic).

**What it costs at scale.** The tick's *average* cost is linear in clients — ~37 µs per client per tick for a
27,697 B frame, i.e. the kernel copying the frame at ~748 MB/s ([#147 §6], [derived]) — but the tick's *worst case*
is not average: **one stalled client can hold the tick for the full 100 ms timeout**, because the send is inline and
blocking — and because the sends are **sequential** under one mutex, N stalled clients can hold the tick for N × 100 ms.
So the fan-out's scaling limit is the **stall policy**, not the throughput: at 80 clients the average send is
2.96 ms/tick ([derived] 80 × 37 µs) — 3 % of the current 115 ms tick, 7 % of a 44.6 ms one — while the tail is still
100 ms *per stalled client*.

### L2 — The data we want is borrowed from the vendor and released at the end of the tick

**Evidence.** `obscan_scan_get_model_point_color_clouds` fills a *borrowed* cloud (`main.cpp:1226`); the tick
decimates it into its own `xyz` vector (`main.cpp:1281-1292`) and calls `obscan_scan_model_clouds_release` at the
end of the tick (`main.cpp:1328`) without ever copying the full cloud ([#136 stage 2]). The cloud reached
**1,366,566 points** by the run's end — 16.4 MB as xyz, 38 MB with normals and colours ([#136 stage 1]).

**What it costs at scale.** The tick's structure is forced: *read → copy out → release*, all inside one tick, because
the buffer is only valid until the next publish. "Holding the full cloud" therefore means **copying** it: 16.4 MB
per copy at the run's end, and the cloud grows during the scan, so a full-cloud path at 24 Hz is ~394 MB/s of memcpy
at the run's end ([derived] 16.4 MB × 24). The honest form of this limit is a **copy budget**, not a buffer handle.

### L3 — The data path is decimated, positions-only and ephemeral

**Evidence** ([#136] stages 3–8). The tick keeps **20,000 of 1,366,566 points — 98.5 % dropped per frame** — at
8.7 frames/s (174,000 points/s, 2.09 MB/s). The V1 `cloud_snapshot` has **no colour field**
(`engine/include/openscanner/frame_protocol.h:38`) and the getter's `normals` and `colors` arrays are never read; the
scan is configured `post_texture = OB_SCAN_NO_TEXTURE` (`main.cpp:960`), so the vendor produces no colour to begin
with. The browser accumulates the teardown's 6,323,235 vertices in a `Float32Array` (75.9 MB) and persists nothing
(`ui/app.js:258-267`). The only durable artifact is the **vendor's** `model.ply` (960,974 vertices, 26.9 MB) and
`mainLOMCache.obmc` (657 MB) written into `/tmp/openscanner_live_server_session` — a path we set
(`main.cpp:948`) and never read back.

**What it costs at scale.** The 20,000-point budget is a **choice, not a time constraint** — [#136]'s "where the time
goes" section is explicit: *"reading the full cloud costs the same 0.00 ms (getter); the decimation is a choice made
at `:1183`, not a time constraint"* — [#136]'s line numbering, which predates rows #139/#141; on `5656a6b` the
budget constant is `main.cpp:1235`. So the limit here is a *product* limit (what we are willing to carry), not a
machine limit. And because the record carries the same decimated buffer, the 1 GiB record cap is **4,473 frames ≈
8.6 minutes** of a scan ([#136 stage 5]) — the archive is a wire capture, not a save.

### L4 — The pacing is a decision and a platform fact, not the code limit

**Evidence.** The live tick waits `CFRunLoopRunInMode(default, 0.1, false)` (`main.cpp:1130`). A zero-work control
measures what that wait actually costs on this machine: **103.19 / 103.75 / 103.75 ms** for a 100 ms request
([#147 §5]) — so the **live tick's ceiling is 9.68 Hz**, and [#147 §9] records that #112's measured 8–9 Hz is *at*
that ceiling. The wait is **99.4 % of the tick** ([#141]'s own instrument, as reported in [#147 §9]); the untimed
residual is 0.01 ms ([#147 §3]). The source is **24.0 Hz** ([#151]).

**What it costs at scale.** The wait is where the tick's time goes, and it is *ours to set*: our 100 ms pace buys
9.68 Hz, and the source can feed **24.0 Hz** ([#151]) — a **2.8× headroom**, of which **2.57× is reachable by
changing the pace alone** (a 41 ms wait realises 44.62 ms → 22.4 Hz; a 20 ms wait realises 24.29 ms → 41.2 Hz,
[#147 §5, §14]). No encoder or broadcast work moves any of that: at 22.4 Hz the tick's work would be 4.4 ms of
CPU per second ([derived]). So the cadence is **#113's decision** plus the platform's floor — it is *not* the code
limit the other readings hit, and the 50 fps the vendor advertises is the raw frame rate, not the fused cloud's.

### L5 — The two monoliths, and where their seams are

**Evidence.** `tools/scan_server/main.cpp` is 1,871 lines and carries the synthetic server, the live session
lifecycle, the tick, the plane removal, the recorder, the status JSON, the teardown snapshot and the CLI
(`main.cpp:1100-1363` is the tick alone). `tools/raptor_client/main.cpp` is 2,526 lines and carries the msgpack
codec (`:185-241`), the RPC client (`:304-457`), the replay loop (`:2119-2213`), the live loop (`:959`), the capture
(row #140, `:698-757`), the frame-array analysis (`:773-866`) and the selftests. `engine/` is already modular.

**What it costs at scale.** The measured hot paths sit in *pure* functions with no device dependency: the encoder
(`frame_protocol.h:39-73`) is 63 % of the tick's CPU ([#147 §7]) and the msgpack `Reader::bin`/`next`
(`raptor_client/main.cpp:234-241`) is 50 % of the replay client's CPU ([#147 §4.1]). Both are testable without a
scanner, and both are the kind of code that gets *worse* when it shares a file with a device lifecycle. The seam
that the profile argues for is therefore not "split the file by line count" but **split it at the three boundaries
the measurements expose**: frame production (fetch → decimate → encode → a V1 buffer), transport (the WS server, and
eventually a writer thread), and persistence (the recorder).

---

## 3. What "10× capacity" actually means here

"10×" is not one thing, and the limit that binds **depends entirely on which one is meant**. But first, the number
that reframes all of them.

### The headroom we already have

| | rate | where it comes from |
| --- | --- | --- |
| the device's **fused cloud** (what we could publish) | **24.0 Hz** | 8,119 device frames over 338.6 s in the #112 log ([#151]) |
| **our publish rate** | **8.7 Hz** | our 100 ms tick ([#151]) |
| the ceiling of **our current pace** | **9.68 Hz** | the run-loop floor: `CFRunLoopRunInMode(0.1)` realises 103.19–103.75 ms ([#147 §5]) |
| what the **pacing mechanism** can do when asked for the source's rate | **22.4 Hz** | a 41 ms wait realises 44.62 ms ([#147 §5]) |

So the publish headroom is **24.0 / 8.7 = 2.8×**, and **2.57× of it is reachable by changing the pace alone**
(8.7 → 22.4 Hz) — the remaining 7 % is the platform's timer. The 8–9 Hz we publish is **our pacing, not the
source** ([#151]), and the vendor's advertised **50 fps** is the *raw* frame rate: the fused cloud is 24 Hz, and
whether 24 Hz is itself the fused rate rather than the raw one is [#151]'s open question.

**The tick cadence is therefore not a code limit.** At 22.4 Hz the tick's work would be 0.195 ms × 22.4 = **4.4 ms
of CPU per second** — 0.44 % of one core ([derived] from [#147 §3]). The cadence is a **decision** (#113's) plus a
**platform fact**; the code limit is elsewhere, and the readings below say where.

### The ingest's per-frame cost, and the budget left for logic

The operator's requirement is that this ingest become *"extremely efficient so it has very low latency so we can run
near real-time compute and logic on this data"*. That is a question about the **ingest's cost against the source's
frame budget**, and the measured numbers answer it.

**What the ingest costs today** (the live tick, from #112's segments and #147's device-free profile):

| part of the tick | cost | where it comes from |
| --- | --- | --- |
| the vendor's publish + our getter | **0.00 ms** (mean; max 0.18 ms) | the `getter` segment in #112's summary line; [#136]'s "where the time goes" says the same |
| the decimate + the encode of the broadcast's points | **1.18 ms mean, 5.05 ms max** over 2,522 ticks | the `post` segment in #112's summary line |
| the broadcast | **0.05 ms mean, 0.96 ms max** (1 client); 0.29–0.31 ms p50 (8 clients) | #112's summary line; [#147 §3] |

**The `post` segment's cost is linear in the point count, and the live measurement says what a point costs.** The
per-window p50s in #112's raw log run 0.04 ms at 862 points, 0.09 at 2,103, 0.19 at 4,801, 0.35 at 9,527 and 0.68 at
17,943 — a fit of **37.5 ns per point** [derived], i.e. **0.75 ms at the 20,000-point budget**. The run's *mean*
(1.18 ms over a mean of 16,494 points) is higher than that because of a heavy right tail (max 5.05 ms).

**One honest disagreement between the two datasets, stated because it changes which number to budget with.** The
device-free profile attributes **63 %** of the tick's CPU to the *encoder alone* — **53.4 ns/point**
([#147 §3]+[§7], [derived]) — which is *more* than the live path's whole `post` segment costs per point (37.5 ns).
The two are not reconcilable from the data in hand: the synthetic tick's encoder is a different code path on a
different point count, and the attribution is by leaf frames. **The budget below uses the live figure** (0.75 ms
typical, 1.18 ms mean), because it is the one measured on the path that ships; the synthetic figure is the
*conservative* one, and it is the reason S1 (the encoder) is worth doing at all.

**So, against the source's frame budget:**

| source rate | frame budget | the ingest, typical (0.75 ms) | left for logic | the ingest, the run's mean (1.2 ms) | left | + the publish at 8 clients (1.5 ms) | left |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **24.0 Hz** | 41.7 ms | **1.8 %** | **41.0 ms** | **2.8 %** | **40.5 ms** | 3.6 % | 40.2 ms |
| 50 Hz | 20.0 ms | **3.8 %** | **19.3 ms** | **5.9 %** | **18.8 ms** | 7.5 % | 18.5 ms |
| 60 Hz | 16.7 ms | **4.5 %** | **15.9 ms** | **7.1 %** | **15.5 ms** | 9.0 % | 15.2 ms |

**The answer is yes: the code is fast enough to be the start of that pipeline, and it is not close.** At the source's
own rate the ingest uses **1.8 %** of the frame typically (**2.8 %** as the run's mean) and leaves **~40 ms** per
frame for logic; at 60 Hz it still leaves **~15 ms**. (That is the *floor* answer. The operator's standard is higher
— *"as fast as possible"*, for a lower-end target — and §4.1 prices the gap between what the ingest costs today and
what the zero-waste version costs.) And the reason is structural rather than lucky: **the cloud is
already in hand at zero measured cost** (the getter is 0.00 ms), and the tick's measurable work is the cost of
*publishing a preview* — 0.75 ms of encoding for 20,000 points — not of ingesting.

Two caveats, both of which move the answer the wrong way and therefore belong here:

* **This is the ingest *as configured*.** The 0.75–1.18 ms buys a 20,000-point *preview*. If a consumer needs the
  full cloud, the cost moves into the ingest as a **copy** (16.4 MB at 1.37 M points = 394 MB/s at 24 Hz, L2) — or it
  costs nothing at all if the consumer reads the SDK's **borrowed** buffer before the release (`main.cpp:1328`).
  That choice is C1's (§4.1).
* **The fan-out rides on the same budget.** At 8 clients the publish adds 0.29 ms, and at the source's rate the
  broadcast's *stall* (100 ms per client) is 2.2× the whole tick — so the publish path has to come off the ingest
  before the budget above is real (S3, and C1's slot).


### A. 10× the points delivered to the UI (20,000 → 200,000 per frame)

* the encode: **37.5 ns/point** (the live `post` fit above) × 200,000 = **7.5 ms** per tick — or **10.7 ms** at the
  conservative synthetic figure of 53.4 ns/point ([derived] 123 µs of encoding for 2,304 points, [#147 §3]+[§7]);
* the broadcast per client: 2.4 MB per frame at the measured ~748 MB/s = **3.2 ms** per client, so **25.6 ms** at
  8 clients ([derived] from [#147 §6]);
* the tick's work ≈ **33–36 ms** (the live or the conservative encode) — against the current pace's 115 ms budget
  that fits, but against the source's rate (a 44.62 ms realised budget, [#147 §5]) it is **74–81 % of the tick at
  8 clients**, and 31 % at one client.

**The binding limit is the fan-out** — the server's synchronous broadcast — and the fixes are S2 + S3, not a faster
machine. The source and the consumer are not what binds (see the consumer subsection below).

### B. 10× the clients (8 → 80)

* the average send: 2.96 ms/tick ([derived] 80 × 37 µs) — 3 % of the current 115 ms tick, 7 % of a 44.6 ms one;
* the tail: **100 ms per stalled client**, and the sends are sequential under one mutex
  (`ws_broadcast_server.cpp:503-517`), so a stalled client is **2.2× the whole 44.6 ms tick** at the source's rate.

**The binding limit is the stall policy, not throughput** — and it binds at *one* stalled client, not eighty. Only
S3 changes it.

### C. 10× the capture size (1.37M → 13.7M points per scan)

* the vendor's own write scales with it: `mainLOMCache.obmc` 657 MB → **6.6 GB**, `model.ply` 26.9 MB → **269 MB**
  ([#136 stage 8]);
* a full-cloud copy in our pipeline: 164 MB per copy at 24 Hz = 3.9 GB/s of memcpy ([derived], L2).

**The binding limit is disk** — and it binds *first*, because the vendor writes that file whether or not we ask it
to. #149's "bounded by disk" is the constraint, not a detail.

### D. Publish the source's full rate (8.7 → 24 Hz)

This is the reading the brief used to call "10× the scan rate", and it needs restating: **the source cannot be
published 10× faster than it arrives.** The meaningful version is *publish everything the source produces*, and
against that:

* the cadence is **not** the limit — the mechanism reaches **22.4 Hz** when asked ([#147 §5]);
* the tick's work at 22.4 Hz is 4.4 ms/s of CPU ([derived]) — 0.44 % of a core;
* the broadcast at 8 clients is 25.6 ms of a 44.6 ms tick (57 %) — the same 57.7 % share the profile measured
  ([#147 §6]);
* one stalled client (100 ms) exceeds the whole tick.

**The binding limit is the synchronous broadcast, then the stall** — S2 and S3 — and the cadence is a decision
(#113) rather than a constraint. The **50 fps** the vendor advertises is not a target for this pipeline: it is the
raw frame rate, and the fused cloud we publish is 24 Hz.

### E. 10× the scans per day

* each session is bounded at 300 s (`main.cpp:669`) and leaves ~684 MB of vendor artifacts behind ([#136 stage 8]);
  10× the scans/day is ~6.8 GB/day of `/tmp` churn that macOS may clear.

**The binding limit is the teardown and the disk**, not the tick.

### The consumer: where it binds, and where it does not

The browser is worth separating carefully, because its two costs sit in different places:

* **The live path is lossy by design and absorbs a rate increase as drops, not as lag.** The UI keeps the latest
  frame and *drops* a live frame when the renderer is behind (`ui/app.js:298`), and it holds **one** decoded
  20,000-point cloud (240 KB) that is replaced, not accumulated (`ui/app.js:305`). In the #112 conditions-4–6 run
  the HUD's `data` fps (5.8–10.0) tracked the **server's** publish rate (6.1–8.5/s) while `render` ran at 59.5–60.1 fps
  (`tests/results/2026-10-19-plan112-conditions-4-6-hud-capture.md:39-46`) — the consumer
  was not the limit even at 8.7 Hz, and at 24 Hz it would still be 2.5× under the render loop (60 / 24). So the consumer does
  **not** bind the publish rate; it caps the *useful* rate at the renderer's, and the cost of exceeding it is
  dropped frames.
* **The teardown path is where the page's memory goes**: the full-detail chunks are accumulated into one
  `Float32Array` of 6,323,235 × 3 floats = **75.9 MB** (`ui/app.js:259`, [#136 stage 7]) — a **one-shot** cost at
  teardown, bounded by the mesh's size, not by the publish rate.

**So the ordering of limits, across the readings: the fan-out and its stall (server side) → the source's own 24.0 Hz
ceiling (and the 50 fps raw rate beyond it) → the browser's render loop and the teardown's 75.9 MB (consumer side) →
disk (the capture size) → the teardown (scans/day).** The tick cadence is not on that list as a *code* constraint:
it is the one lever that is currently set to 100 ms, and moving it is #113's decision, not an optimisation.

### The plain statement

**The pipeline is not CPU-bound today** — the tick spends 0.53 % of itself on work ([#147 §3]) — and it is **not
rate-starved either**: the source can feed 24.0 Hz and we publish 8.7 Hz ([#151]), a 2.8× headroom that is mostly a
pace change. So "10× capacity" mostly does **not** need faster code. It needs the seams that let the **fan-out**
and the **data volume** move independently of the **cadence** — and, under every reading, the first thing that binds
is the **synchronous broadcast** (57.7 % of the tick's CPU at 8 clients, with a 100 ms stall inside the tick
thread), not the source, not the consumer, and not the cadence.

**But "not the binding limit" is not the same as "as fast as possible", and the operator's standard is the second.**
§4.1 names and prices the wastes that are *inside* today's cheap ingest — the per-byte encoder, the per-tick
allocations, the two syscalls per client, the every-tick vendor re-assert — and says which of them a lower-end target
cannot afford. The pipeline is not CPU-bound; it is **not yet zero-waste**.

**Adaptivity is the same property seen from the hardware side** (§4.2): the tier is a *preset* that rides with the
data, the ladder sheds quality rather than frames, the bounds are chosen rather than inherited, and the zero-waste
work is what makes the low tier reachable at all.

---

## 4. The seams worth cutting first, ranked

Ranked by (measured cost) ÷ (behaviour risk), with the evidence for the order. Each seam states what it buys, what it
risks, and what must **not** change.

### S1 — The encoder: `push_back` per byte → one `memcpy`

* **What it buys.** The encoder is **63 % of the tick's CPU** with no client ([#147 §7]: 67 ms of 106 ms in a 20 s
  Time Profiler run) — the single largest measured item in the tick. Making it a `memcpy` takes the tick's work from
  195 µs to ~72 µs ([derived]) and the live path's derived ~1.06 ms of encoding per 20,000 points to ~0.1 ms. It is
  also the **precondition for every budget** in row #152: with the work at 0.2 % of the tick, the budgets become
  statements about the *cadence* and the *fan-out* instead of about a timer.
* **What it risks.** The output must be **byte-identical** — the UI decodes it (`ui/frame_protocol.js`) and the
  recorded archives contain it. Mitigation: a golden-vector equivalence test against the current encoder, over
  random point counts and payloads, device-free.
* **What must not change.** The V1 wire format; the fact that this is *our* header
  (`engine/include/openscanner/frame_protocol.h`), not the contract header.

### S2 — The WebSocket send: two syscalls per client per frame → one

* **What it buys.** `SendFrame` writes the 2–4 B header and then the payload separately
  (`ws_broadcast_server.cpp:255-256`); one write halves the syscall count per client per frame. **Honest accounting:**
  at 27,697 B/frame the *byte copy* dominates (the measured ~748 MB/s, [#147 §6]), so the win is the per-syscall
  overhead, not the bytes — a fraction of the 57.7 %.
* **What it risks.** Nothing on the wire: the same bytes in one write are still one RFC 6455 frame.
* **What must not change.** The RFC 6455 framing; the V1 payload.

### S3 — The broadcast's shape: a latest-frame slot + a per-client writer thread

* **What it buys.** The network leaves the tick's critical path: the tick's cost becomes **O(1) in clients** instead
  of O(N), the 100 ms stall stops being a tick event, and the drop policy becomes explicit rather than a side
  effect of a failed send ([#147 §6]; [#136]'s note that decoupling the fetch from the broadcast "would let the tick
  carry the full cloud without touching the 100 ms floor"). At the source's rate a 44.6 ms tick is the unit, and
  **one stalled client (100 ms) is 2.2× the whole tick** — so what breaks the cadence is a slow client, not the
  rate.
* **What it risks.** Three things, and the first is a real constraint the map does not show:
  1. **The traffic is not uniform.** The teardown's full-detail mesh chunks (`main.cpp:1406-1424`, 317 chunks of
     20,000 vertices) must arrive **in order and complete** — the page accumulates them between `full_mesh_start` and
     `full_mesh_done` (`ui/app.js:258-267`). A latest-frame slot cannot carry them. The seam needs **two classes of
     traffic**, and the class has to be explicit in the API.
  2. **Concurrency.** A producer → slot → per-client writer contract is a data race if it is wrong, and a data race
     here is a crash, not a slowdown.
  3. **The client-drop semantics change.** Today a failed send drops the *client*
     (`ws_broadcast_server.cpp:506-513`); with a slot, a slow client loses *frames* instead.
* **What makes it cheaper than it looks.** The UI is **already latest-wins**: `if (framePending) return; // drop live
  frame: renderer is behind` (`ui/app.js:298`). A server-side drop-oldest policy would match the client's existing
  one — but only for the live snapshots, not for the mesh chunks.
* **What must not change.** The V1 frame's contents; the tick's *pacing* (the wait at `main.cpp:1130` and the fetch
  at `main.cpp:1226` stay in the tick); the clean-room posture (all of this is our own code).

### S4 — Persistence: a project-owned archive, written off the tick

* **What it buys.** It closes the gap [#136] called "the one that matters most" — the project artifact is written
  where nobody can find it, and nothing reads it back ([#136 stage 8]) — and it is the only seam that changes what the
  product *keeps*. It is row **#149**'s row; this note does not duplicate it, it places it.
* **What it risks.** The disk budget (657 MB + 27 MB per scan today, growing with scan time, [#136 stage 8]); the
  format decision, which belongs to the operator; and — the structural point — **writing on the tick would show up in
  the tick's work**, which is currently 0.01 ms of residual and 195 µs of CPU ([#147 §3]). So it must be a separate
  writer, which makes it the *same shape* of change as S3.
* **What must not change.** The ABI. The vendor's own write stays the vendor's — but **where** it is written is
  ours to choose (`main.cpp:948`), and that choice becomes a one-way door for existing runs.

### S5 — Buffer ownership: the full cloud outside the tick

* **What it buys.** The ability to carry the *full* cloud — 1.37M points with normals and colours ([#136 stage 1]) —
  in our own pipeline, which [#136] is explicit is **not** a time constraint (the getter is 0.00 ms) but a copy-budget
  one (L2).
* **What it risks.** The copy budget itself: 16.4 MB per copy at the run's end, growing with the cloud, and the
  copy is what makes it expensive (394 MB/s at 24 Hz, [derived]). The seam's real form is a **budget**, not a
  handle. And it must not be built before S4 exists — there is no point copying 16 MB per frame before we know where
  the data is going.
* **What must not change.** The ABI's ownership contract. The header states the buffers are borrowed; changing that
  is a **recon** task, not a builder's, and no step here needs it.

### The two capability seams (they cost nothing today and buy everything downstream)

S1–S5 are **cost** seams: each of them removes measured work. The operator's requirement needs two more, which are
**capability** seams — they buy no CPU today, and without them the pipeline cannot exist at all.

#### The shape problem, as it stands

The tick is one thread (`main.cpp:1100-1363`) that does, in order: the **fetch** (`:1195`), the **decimate to
20,000** (`:1204-1261`), the **encode** (`:1264`), the **broadcast** — inline, under the mutex (`:1268`) — the
**record** (`:1273`), the **release** (`:1297`), and then the **pace** (`:1115`). Three facts about that shape matter
for near-real-time logic:

1. **The only path to the data is the decimated broadcast frame** — 98.5 % dropped, positions-only, no colour
   ([#136] stages 3–4). A consumer on that stream sees **1.5 %** of the cloud. Worse for identity: the decimation
   is a **stride over a growing cloud** (`stride = ceil(num_points / 20000)`, `main.cpp:1277-1280` — 69 at
   1.37 M points), so as the cloud grows the stride changes and the sampled set shifts — **the decimated stream is
   not stable frame-to-frame**, which disqualifies it for anything that needs to recognise the same point twice.
2. **The network is inside the ingest's critical path** — the fan-out is inline, sequential, under `clients_mutex`,
   with a 100 ms per-client timeout ([#147 §6]). At the source's rate (a 44.6 ms tick) one stalled client is 2.2× the
   tick.
3. **The latency covers only the last leg.** The V1 envelope carries two clocks — `t_capture` and `t_server`
   (`frame_protocol.h:33-34`) — and the live path fills `t_capture` at the **encode**, not at the ingest
   (`BuildFrame(frame_id++, NowMs(), …)` → `encode_cloud_snapshot(frame_id, capture_timestamp_ms, NowMs(), …)`,
   `main.cpp:1295` and `:501-502`). **Row #142 has since put the server's clock in the status message
   (`server_now_ms`, `main.cpp:491`) and the arithmetic in `ui/frame_age.js`** — and #142's own comment is explicit
   that what it reports is the **tick → render** age, because `t_capture` is the tick's *read* time. So the leg still
   missing is the **ingest** one: `t_capture` should be the instant the cloud was *read* (`main.cpp:1226`), not the
   instant the frame was *encoded* — 0.75 ms apart today, and the hook for the device's own capture time if the ABI
   ever exposes one on the model-cloud path (it does not: the ABI's timestamp getters are on the frame/image paths,
   `obscan_api.h:924`/`:950`).

#### C1 — the ingest/consumer seam

* **The tick produces a frame object per ingest**, carrying: the cloud **view** — the SDK's borrowed pointer and its
  point count, valid until the release at `main.cpp:1328` — or a shared copy of whatever the consumers were
  configured to need; the decimated broadcast buffer; and a **per-stage timestamp set** (ingest, decimate, encode,
  publish).
* **It is published to a latest-frame slot** — single-writer, overwritten, never blocking — which the consumers read
  and the network writers drain. Consumers drop older frames; that is the policy the UI already applies to live
  frames (`ui/app.js:298`), so the server-side policy matches the client's existing one.
* **The envelope's `t_capture` is filled with the ingest instant**, not the encode instant, so the age the page
  reports starts at the data's arrival — with **no protocol change**, because the field is already there and #142 has
  already built the clock-offset half (`server_now_ms` + `ui/frame_age.js`).
* **The consumers never block the ingest.** The ingest overwrites the slot and moves on; a consumer that falls behind
  loses frames, which is the same loss the UI already accepts.
* What this buys: the four named consumers get the cloud **as it arrives**, at full resolution or at a resolution
  each of them configures, and the ingest's budget stays at the 0.75–1.2 ms measured above.

#### What "fast enough" means, for the four named consumers

The operator named four: **tracking, marker ID, ellipse transformations, exposure settings**. They are not four
consumers of the same thing, and the table below is what the measured budget and the code say about each. Their own
compute budgets are not set here — those are new stages, and #152's per-stage budgets are where they belong; what is
settled here is the **data** each one needs and **where it runs**.

| consumer | what it needs from the frame | the data volume it needs | the budget it must fit | where it runs |
| --- | --- | --- | --- | --- |
| **tracking** | the scanner's pose over time. The pipeline **already carries a pose as its own message type** (`MessageType::Pose`, `frame_protocol.h:20`, encoded at `:109`, broadcast every tick at `main.cpp:1206`) | the **pose**, not the cloud — unless it tracks surface features, in which case it needs a *stable* subset, and the stride decimation is not one | the whole 40.5 / 18.8 / 15.5 ms left (§3); a pose consumer is microseconds | a consumer thread |
| **marker ID** | **identity across frames** — the same physical point in successive frames | the full cloud, or a **stable key**: the vendor already fuses at a **0.1 voxel** ([#136] stage 0), and a voxel grid stays stable as the cloud grows, unlike the stride | its own compute; the *data* cost is the copy budget (394 MB/s at 24 Hz, L2) or a borrowed read before the release at `main.cpp:1328` | a consumer thread, reading C1's slot |
| **ellipse transformations** | the region to transform, at the resolution the transform is defined at | the full cloud, or a region of it — the decimated stream changes the fit's *input*, not just its size | as above | a consumer thread |
| **exposure settings** | the current exposure value, and the ability to change it | not a cloud consumer at all: a **device-parameter** consumer. The tick re-asserts the exposure every tick (`main.cpp:1176`) and #153 proposes to make that on-change | the tick's own `exposure` segment ([#141]'s instrument) — and the apply must not block the tick | it must **act on** the tick: a bounded "requested value" slot — #153's dirty-flag shape — not a run in the ingest thread |

**The rule that makes the table work: none of the four may run in the ingest thread.** The ingest is the pipeline's
clock — the tick's pace *is* the pipeline's cadence — so a consumer that takes 40 ms would stall every other consumer
behind it and would show up as a cadence failure, not as a slow consumer. The exposure consumer is the one that must
act *on* the tick, and it does so through a bounded slot rather than by running inside it.

#### C2 — the pacing primitive: a deadline-based wait, and a requestable rate

* Today the wait is **relative** and the period is a **compile-time constant**: `CFRunLoopRunInMode(kCFRunLoopDefaultMode,
  kLiveTickIntervalMs / 1000.0, false)` with `kLiveTickIntervalMs = 100` (`main.cpp:1130`, `:663`).
* Measured: a 100 ms request realises **103.19–103.75 ms**, a 41 ms request **44.62 ms**, a 20 ms request
  **24.29 ms** ([#147 §5]) — the platform's overshoot (3.2–4.3 ms) is charged **per wait**, and a relative wait
  accumulates it.
* A **deadline-based wait** (`sleep_until(t0 + n × period)`, the deadline advanced by the *period* rather than by the
  elapsed time) turns that into a **bounded** error: a late tick does not push the next one out, and the realised
  rate converges on the requested one instead of drifting.
* It also makes the rate **requestable**: the period becomes a parameter, so #113's "a tick with comfortable margin
  above the device's mode rate" becomes a *setting* rather than a rebuild — and it is **measurable with what is
  already on main**: the instrument reports `nominal_wait_us` and the `overshoot` distribution
  (`perf_window.h:143-146`), so a requested rate's realised rate is a report, not an estimate.
* Two caveats. (a) The platform's overshoot does not go away: a 50 Hz request (20 ms) still realises ~24 ms (41 Hz),
  so the **request** must be set below the target and the overshoot reported. (b) Our code registers **no run-loop
  source or timer** (no `CFRunLoopTimer`/`CFRunLoopSource` anywhere in `engine/`, `mesh/`, `tools/`, `ui/`), so the
  run-loop wait has nothing of ours to service — but it would also service any source the **SDK or the OS**
  installed on the worker's run loop, which a `sleep_until` would not. The migration step must check that before
  switching.
* The stop path is unaffected: the tick tests `g_stop_requested` at the top of every tick (`main.cpp:1116`), so the
  wait is already the worst-case stop latency.

### The performance budget: what each stage costs, what it must cost, and the gate

**The standard is not "fast enough at 24 Hz".** The operator's words: *"I want this not to be fast enough — I want this
part of the core architecture to be as fast as possible. Our ability to extend the features of Creality Scan and get it
running on lower-end hardware relies on this pipeline."* So performance here is not an observation, it is a
**constraint on the design**, expressed as a **budget per stage** and enforced by a **gate**. Row #152 builds that
contract and its harness; **this note is the architecture the contract enforces** — the waste list below is the set of
things the budget must forbid.

**The lower-end target — an assumption, to be replaced by a named machine.** The reference profile is the M1 16 GB:
the vendor's stated macOS minimum and the machine every number in this note was measured on ([#147 §2.1]; [SPEC]'s test
hosts). **The operator will name the lower-end target; until then this note assumes a machine at half the M1's
single-core throughput and half its memory bandwidth — a factor of 2 on every per-frame time and on every
byte-moving term.** Every figure below is given at the reference host and at that factor, so the reader can see which
costs the factor hurts and which it does not.

#### Today's wastes, named and priced

| # | the waste | where | what it costs today | the zero-waste form (exact) | cut? |
| --- | --- | --- | --- | --- | --- |
| **W1** | **per-byte writes** — the encoder pushes 4 bytes per float: 240,000 `push_back`s per frame | `frame_protocol.h:39-73` (the `put_f32` lambda at `:55-58`) | **0.75 ms** of the ingest at 20,000 points (the live fit above), **1.07 ms** at the synthetic 53.4 ns/point | **one sized write of 240,036 B** — an exact byte count, no instruction-count term | **yes** |
| **W2** | **per-tick allocations** — the preview's `xyz`, the encode buffer, the pose buffer, one WS header per client | `main.cpp:1281`, `:1264`, `:1172`; `ws_broadcast_server.cpp:241` | **4 allocations and 480,196 B per tick** (4.2 MB/s at 8.7 Hz, **11.5 MB/s at 24 Hz**) | **0 allocations per tick** — reusable buffers | **yes** |
| **W3** | **the two-vector copy** — the synthetic path copies the sphere and rotates the copy | `main.cpp:610-611` | ~4 % of the tick's CPU in the synthetic path; the live path has no such copy (it decimates the vendor's buffer in place) | a rotate **in place**, into the reusable buffer of W2 | **yes** (synthetic path only) |
| **W4** | **two syscalls per client per send** — the header, then the payload | `ws_broadcast_server.cpp:255-256` | 2 per client per tick; the *bytes* dominate the measured 37 µs per client ([#147 §6]) | **1 write per client per frame** — the header in front of the payload | **yes** |
| **W5** | **a per-tick vendor re-assert** — the exposure used to be re-sent every tick with an unchanged value | was `main.cpp:1151` on `efe9253` | **1 vendor call per tick**; **never timed by a measurement** (#141's `exposure` segment was still to run) | **1 call per change** | **done** — row **#153** landed while this note was being written: the push is now conditional (`exposure_apply.h`, `main.cpp:1170-1176`), so the waste is gone from the code and the sheet keeps the *pattern* |
| **W6** | **a syscall per tick whose answer cannot change** — the orphan guard compares `getppid()` to the parent captured at startup | `main.cpp:1109-1114` | ~1 µs — **0.001 %** of the live tick's ~103 ms period here; a syscall nonetheless | **0** — check it on a timer, not every tick | **yes** |
| **W7** | **copies of the cloud** | — | **0 today** (the tick decimates the vendor's buffer in place) — but **16.4 MB per frame** (394 MB/s at 24 Hz) the moment a consumer takes the full cloud | a **shared view**, not a copy | **the seam (C1) is what avoids it** |

**And what cannot be cut, with the reason:**

* **The pacing wait.** C2 makes the rate *requestable*; it does not make the wait cheaper. It is the schedule, not
  waste — 99.4 % of the tick ([#147 §9]).
* **The getter.** 0.00 ms, and the vendor's own fusion on the SDK's threads is what sets the 24.0 Hz source ([#151]).
  It is the data's arrival, not our work.
* **The bytes on the wire.** 27,697 B per client per frame is inherent to broadcasting a 20,000-point cloud to N
  clients. The only levers are the decimation choice (#136c — a *product* decision) and the client count.
* **The one preview build per frame.** If the preview exists, it must be built once. Zero-waste means *build once,
  reuse the buffer* — not *do not build it*.
* **The single-threaded ingest.** fetch → decimate → encode is one sequential dependency; splitting it would add
  synchronisation cost, not remove it. The parallelism belongs to the **consumers** (C1), not to the ingest.

#### What the lower-end target does to each waste

| | at the M1 | at the target (×2) | what the factor does |
| --- | --- | --- | --- |
| **W1** the per-byte encode | 0.75 – 1.07 ms/frame | **1.5 – 2.1 ms/frame** | a per-byte write is an **instruction-count** term, so it degrades with the core's speed; the zero-waste form is a **bandwidth** term, so it degrades with the memory system instead. **The gap widens on a slower machine** — which is exactly why the waste is what the lower-end target cannot afford |
| **W2** the per-tick allocations | 480 kB/frame, 11.5 MB/s at 24 Hz | **23 MB/s** | the memory-traffic term a low-end memory system feels first |
| **W4** the send | 37 µs per client per tick | **74 µs** | the syscall overhead doubles with it |
| **W5** the exposure re-assert | **0 in the steady state** (row #153: a push per change) | 0 | a push per *change* is not a cost; the *pattern* — a per-tick re-assert of a value the device already holds — is the one to watch for elsewhere |
| **the ingest as a whole** | 0.75 – 1.2 ms of a 41.7 ms frame (1.8 – 2.8 %) | **1.5 – 2.4 ms (3.6 – 5.8 %)** | the *budget* survives the factor; the *waste* inside it does not shrink |

#### The gate: "as fast as possible" as a line that cannot drift

The budget is enforced in two layers, and the first is the one that makes the property real:

* **The invariants** — machine-independent, exact, and assertable on *every* build:
  **0 allocations per tick** (W2), **0 per-byte writes** (W1), **1 write per client per frame** (W4), **0 vendor
  re-asserts per tick** (W5), **0 copies of the cloud** (W7), and **no syscall per tick except the wait** (W6).
  These are not "performance targets" — they are facts about the code, and a regression in any of them is a defect, not
  a slowdown.
* **The times** — per stage, against #152's reference profile and its safety factor. Every threshold is a
  **measurement**, because every number in this note is one ([#147]'s method is the harness: the synthetic server, the
  loopback mock, the profiler, `vmmap`). #156's end-to-end number is the complement: the gate's per-stage numbers are
  the parts, its total is the whole. And the budgets are **per tier** (§4.2): the times are stated for a tier, while
  the **invariants hold at every tier**.

**So performance is a first-class property of this path in a specific, checkable sense:** the ingest's cost is a
constraint the design must satisfy (the waste list), the constraint is exact and assertable (the invariants), and the
gate is row #152's. Nothing in the design is allowed to regress into the waste list without a measurement saying so.

### Adaptivity: a tier carried with the data, bounds by construction, and a ladder that sheds quality

The operator's thesis: *"We also want to be flexible for lower spec hardware. That is one of the benefits of going
faster — we are 'lighter' at lighter loads for less CPU performance or memory size/bandwidth. This is part of why this
project should exist."* So **adaptivity is part of the architecture, not a later optimisation** — and it is adaptivity
*of the shape*, not of the speed: the pipeline must be able to run lighter, and the zero-waste work is what buys the
headroom for it.

The board already owns the *source* of the tier — row **#154** (a startup probe and a preset mapping, with the axes
"publish rate, point count (decimation), colours/normals, capture on/off and the browser accumulation cap"). **This
section is the architecture that makes those presets expressible**; it does not duplicate that row.

#### The tier is data, and it rides with the data

Today the tier is **two compile-time constants**: `kLiveBroadcastPointBudget = 20000` (`main.cpp:1235`) and
`kLiveTickIntervalMs = 100` (`main.cpp:670`). The operator's point is that they must be *knobs* — and that the knob
must travel with the data, so that the network and the consumers adapt **without the ingest path special-casing
hardware**.

* **The tier descriptor** is a small struct the frame object (C1) carries: the delivered **points** and **bytes**, the
  **flags** (`has_colour`, `has_normals`, `is_decimated`), the **source** point count (so a consumer knows it is
  looking at 1.5 % of the cloud), and the **frame interval** (the rate — a consumer cannot budget a latency without
  knowing the cadence it is being fed at).
* **Two of its axes are already on the wire**: the frame's `count` and the envelope's `payload_len`
  (`frame_protocol.h:28-36`). The missing pieces — the source count and the cadence — ride in the **status message**
  (the flat JSON at `main.cpp:469-496`, already the control/status channel and already per-connection), because the
  tier changes on a *preset* change, not per frame. **No protocol change is needed for the tier as it stands.**
* **The colour axis is a protocol change and is moot until #136b decides it.** The V1 `cloud_snapshot` has no colour
  field (`frame_protocol.h:38`), and the scan is configured `post_texture = OB_SCAN_NO_TEXTURE` (`main.cpp:960`), so
  the vendor produces no colour to carry. "Colours" is a tier axis in the *design* and a constant in the *product*
  today.
* **The rate axis needs C2** — the period is not a knob until the pacing primitive is a deadline-based wait.
* **The ingest never special-cases hardware.** The tier selects a **projection** of the cloud, not a different
  ingest: the projection step (the stride, the build, the encode) is *the same code with a smaller number*. That is
  what "the 20k decimation is a knob, not a constant" means in practice — no branch, no second path — and the cost
  falls with the number, because every term in the waste list (§4.1) is per-point.

**What adaptivity costs, stated plainly: a copy.** The vendor's cloud is borrowed and released at the end of the tick
(`main.cpp:1328`), so a consumer that needs more than the preview needs the ingest to *copy* its resolution into the
slot. Two numbers over one source — the **UI projection** (today 20,000 points, 240 kB) and the **consumer
projection** (whatever the logic needs) — and the second costs what the first does not:

| consumer projection | the copy per frame | at 24 Hz |
| --- | --- | --- |
| 20,000 points (the UI's today) | 240 kB | 5.8 MB/s |
| 200,000 points | 2.4 MB | 58 MB/s |
| the full cloud (1.37 M points) | 16.4 MB | 394 MB/s |

So the tier's two axes are not free, and the ladder below has to say which one it is shedding.

#### Bounded memory and bandwidth by construction

* **The server's per-frame working set** is currently a consequence of the constant: 480,196 B/tick (W2) and
  27,697 B per client per frame. With the tier as a parameter, both become *chosen* numbers rather than inherited
  ones.
* **The page is the low-end failure mode.** The live path holds **one** 20,000-point cloud (240 kB, replaced, not
  accumulated — `ui/app.js:305`), but the teardown's full-detail stream accumulates `msg.total * 3` floats —
  **75.9 MB** at 6,323,235 vertices (`ui/app.js:259`, [#136] stage 7) — and **that number grows with the scan**. The
  page's memory is a function of the *scan*, not of the tier: the one place where the bound is currently *absent*.
* **The bound**: the tier carries an **accumulation cap** for the page (#154's preset axis), and the full-detail mesh
  stream (76.1 MB, [#136] stage 6) becomes a *tier item* — at the low tier it is not sent, and the page keeps the
  20,000-point preview the teardown already produces (`main.cpp:1395`). The page's memory then becomes a function of
  the tier: the preview plus the cap.
* **The network's per-client bytes** are the tier's `bytes`, by construction.

#### The degradation ladder: shed quality, not frames

Under load the pipeline sheds **quality**, never the ingest and never the pose. (The UI's own frame-drop when its
renderer is behind — `ui/app.js:298` — is the consumer side and is *not* the ladder.)

1. **Reduce the point budget.** The encode and the bytes fall proportionally (37.5 ns/point); it is the same code with
   a smaller number; the *frame rate is untouched*. (The ladder's unit is the **preset**, not a continuous knob — the
   stepping and the detection are in the next-but-one subsection.)
2. **Drop the colour axis** (when it exists): the bytes per point fall.
3. **Drop the full-detail mesh stream** at teardown: the page's memory falls from 75.9 MB to the preview.
4. **Reduce the publish rate** (C2): the cost per second falls and the *latency* rises. This is the lever that trades
   the thing the pipeline exists for, so it is **last**.
5. **Fewer clients** — a product decision, and the last of all.

**What can degrade, and what cannot:**

* *Can*: the network projection (points, colours), the publish rate, the mesh stream, the client count, and the
  **consumer projection** — but only when the consumer asks for less.
* *Cannot*: the **ingest** (0.00 ms; everything depends on it and there is nothing to degrade), the **pose stream**
  (160 B/tick; it is what tracking consumes), the **pacing** (the tick's period is the schedule, not a quality knob),
  and the **logic consumers' data** — see below.
* **The logic consumers may need full resolution even when the UI cannot receive it.** The ladder applies to the
  **network projection**, not to C1's slot: the slot carries the cloud, the broadcast carries a *projection* of it. A
  consumer that needs the full cloud is not degraded by the UI's point budget; it subscribes to the slot at the
  consumer resolution, and the price is the copy in the table above. The ladder must not silently reduce what a
  *logic* consumer needs: a consumer's requirement is a **capability request**, and the server answers it within the
  tier's memory bound or says it cannot.

#### The probe: where it runs, and what it must never do

Row **#154** owns the probe and the presets; this is the architecture they plug into, and it settles three things the
row's text does not.

**Where it runs.**

* **At process start, before the device session opens** — the worker's *default* preset is the probe's
  classification. The cost is 1–2 s of startup latency before the first frame, paid once.
* **On demand, out of band** — the operator (or the app) can re-probe when the host changed (a different machine, a
  docked display, a busy neighbour). Never *during* a session: the probe would compete for the CPU and the memory the
  pipeline needs, and it would measure *the pipeline* rather than the host. A re-probe is therefore a
  **pause → probe → resume**, or a probe of an idle host.
* **It is a host measurement, not a device one.** It must be runnable on a machine with **no scanner attached** — that
  is what lets the app answer *"can this machine run Creality Scan here?"* before anything is plugged in — so it
  must not open the device.

**What it must never do**, with the reason this note's own data supplies:

1. **Block a scan.** It is bounded (1–2 s), it runs before the session or out of band, and it is interruptible.
2. **Allocate unboundedly.** A bandwidth sweep that allocates a large buffer can exhaust the machine it is sizing —
   the failure mode of a probe is to break the machine it measured. Its working set is the **pipeline's own**: one
   20,000-point frame (240 kB) plus a bounded loopback buffer.
3. **Touch the vendor SDK.** Not the device context — the SDK's context/session lifecycle is the known crash/hang
   path #89's bounded watchdog exists for, and a *measurement* must not leave the device in a state it cannot recover
   from — and not the loader, so the probe runs with no scanner attached. The probe is **pure host code**: the same
   synthetic path the tick uses, which is device-free by construction.
4. **Measure a busy host.** The numbers must describe the *machine*, not the machine already running the pipeline.
5. **Be a gate.** #154 says it: the probe informs, and the operator may choose above their tier.

**What it measures.** The host (model, cores, RAM, GPU — from the OS, not the SDK) plus a 1–2 s sweep of **the
pipeline's own four workloads** — the encode throughput, the memory bandwidth, the loopback send throughput and the
fan-out writes (#154's list) — **at the pipeline's own size**: a 20,000-point frame (240 kB) encoded and sent over a
loopback socket. A sweep of a different size does not predict the pipeline's behaviour. The method is [#147]'s: the
synthetic server, the loopback mock, the profiler, `vmmap`.

#### The preset: chosen at startup, adjustable at runtime, and how a change propagates

**The separation that makes it work.**

* The **preset** is the *intent*: {publish rate, points, colours, capture on/off, accumulation cap}. A small immutable
  struct, chosen at startup (the probe's classification, or the operator's override) and swappable at runtime.
* The **tier descriptor** is the *fact*: what this frame actually is (points, bytes, flags, source count, interval). It
  is *derived* from the preset and the frame, and it is what rides with the data.

**The ingest's only tier-aware code is its parameters.** The projection reads the preset's `points` (the budget and
the stride) and the tick's period comes from the preset's `rate` (via C2). There is no `if (low_end)` anywhere: a
lower tier is *the same code with a smaller number*, so it is cheaper **because** the per-point work runs fewer times
— which is precisely why §4.1's zero-waste work is what makes the tiers affordable.

**A preset change takes effect at the next frame boundary.** The tick reads the preset **once per tick**, published as
an immutable struct behind an atomic pointer (or as per-field atomics, in the existing `ControlState` style,
`main.cpp:420-427`), so a frame is always produced under **one** preset and a change is never observed half-applied.
That is the same pattern the tick already uses for the exposure and the pause state (`main.cpp:1170`).

**How a change propagates:**

| the change | the ingest | the network | the consumers |
| --- | --- | --- | --- |
| `points` | the next tick's projection | the next frame is smaller | the next frame's descriptor; the status message names the new tier |
| `rate` | the next tick's deadline (C2) | the next frame's interval | the status message (the cadence they are fed at) |
| `colours` | the next tick's flags | the next frame | the next frame's flags — a protocol change, and moot until #136b |
| `capture` | the recorder (S4 / #149) | — | the status message (whether the scan is being kept) |
| `accumulation cap` | — | — | the status message → the page's own policy |

**The latency of a change: the data changes from the next frame; the clients learn the new tier within one status
interval** (~1 s at the live path's cadence, `main.cpp:1333`).

**The command path** is the one the runtime adjustments already use: the flat JSON control channel
(`HandleControlMessage`, `main.cpp:433`), which today carries `set_exposure` and `set_fov`. The UI's override is one
more command on it, and the preset's *effect* rides back out on the status channel.

**The ladder steps between presets, on measured misses.** The ladder is not a continuous knob: it is the **sequence of
presets** (High → Balanced → Lite → Capture-only), and a *miss* is a **measured** event — a stage exceeded its budget
([#152]'s per-stage budgets). The detection point already exists: the tick reports its segments, its overshoot and its
residual ([#141]'s instrument), so a miss is measurable today. On a miss the server steps **down** one preset and
records the step; it does **not** step back up on its own (that would oscillate — the recovery rule is #152's to
decide), and the step is *visible* in the status message, so the UI can say why the quality changed.

**Who may step it.** The operator's choice sets the *starting* preset and, by default, the floor; the ladder's
automatic step is the degradation path. Whether the ladder may step *below* a manual choice is a product decision
(§7): the probe is not a gate, so a manual "High" should not be silently overridden — but a machine that cannot keep up
must also not be allowed to overrun its budget.


#### The low-tier floor, and what is cuttable and what is load-bearing

**The floor, as a design input rather than an outcome.** On the weakest target the pipeline must still:

* **fetch at the source's rate** — the floor is not "fewer frames", it is *the same frames with less per frame*;
* **publish the pose** (160 B/tick) and **keep the frame's age** within the logic's latency budget;
* **hold one tier's worth of memory** — the server's working set and the page's preview plus its cap;
* **keep the scan** if the product is to keep it (S4).

**Cuttable at the low tier:** the full-detail mesh stream (76.1 MB), the colour axis, the point budget, the publish
rate (last), the client count, and the consumer projection (on request).

**Load-bearing at every tier:** the ingest, the projection, the encode, the pose stream, C1's slot, C2's pacing
mechanism, the persistence — **and the waste removals (W1–W7)**. On a target 2× slower with 2× the memory traffic,
today's wastes would consume the frame budget outright: W1 alone is 1.5–2.1 ms of a 41.7 ms frame before any
adaptivity at all. **The zero-waste work is what makes the low tier reachable** — which is why it is the first step
and not a later optimisation.

**The lowest tier is "capture-only"** (a preset in #154's list): the ingest at the source's rate, the full cloud to
disk, and **nothing on the network**. It is the clearest statement of the architecture — the ingest is unchanged, the
tier's network axis is zero, and the product still gets its scan.

### Why this order

S1 is first because it is the largest measured share of the tick's CPU, it changes **no observable behaviour**, and it
needs no device to verify. S2 is second for the same reason at a smaller size. S3 is third because it is the first
*behavioural* change — it needs the traffic classification and a concurrency contract, and it is the one that needs
the UI's drop tolerance to be stated rather than assumed. S4 and S5 are the data plane; S4 precedes S5 because the
archive's format and location decide what S5 is allowed to carry.

---

## 5. The migration order

| step | seam | independently valuable? | reversible? |
| --- | --- | --- | --- |
| **0** | **S1 — the encoder's `memcpy`** | **yes**: the tick's work drops from 0.53 % to ~0.2 % of itself, which is what makes every later budget sayable (#152) and is a regression guard for the wire format (a golden-vector test) | yes |
| 1 | S2 — one write per client per frame | yes: half the syscalls, same bytes | yes |
| 2 | **C2 — the pacing primitive**: a deadline-based wait and a requestable period | yes: the publish rate becomes a *setting* and its realised rate becomes a *report* (`nominal_wait_us` and the `overshoot` distribution are already in the instrument) | yes |
| 3 | **C1 — the ingest/consumer seam**: the frame object, the latest-frame slot, the per-stage timestamps | yes: it is the enabler of all four downstream consumers, and the budget table in §3 only becomes true once the publish is off the critical path | yes — additive, the existing broadcast path is untouched |
| 4 | S3 — the broadcast's shape: per-client writers | yes: the fan-out leaves the tick, and the 100 ms stall leaves with it | **partly** — the drop semantics change (S3 risk 3) |
| 5 | S4 — persistence, off the tick (#149) | yes: the product starts keeping the scan | **no — this is the irreversible one** |
| 6 | S5 — the full cloud in our pipeline | yes: the data hole closes | no (the archive format is now fixed) |

**The order's logic, in one line each.** Steps 0–1 make the tick's *work* negligible, so the §3 budget table is true
at 24–60 Hz; step 2 makes the *rate* requestable; step 3 gives the *data* to the consumers; step 4 takes the
*network* off the tick; steps 5–6 are the data plane, and S4's format decision has to precede S5's copy budget
because it decides what S5 is allowed to carry. C1 comes before S3 because C1 is **additive** (a new slot beside
the existing broadcast) while S3 changes the existing clients' semantics — the cheaper change first.

**The order is also the waste order** (§4.1): step 0 removes **W1** (the per-byte writes, the largest term), step 1
removes **W4** (the second syscall per client), step 3 removes **W2**, **W6** and **W7** (the per-tick allocations,
the invariant syscall, the copy budget) by giving the path a reusable frame object and a shared view, and **W5** (the
every-tick exposure re-assert) was row **#153**'s — **landed while this note was being written**, so the waste is
off the list and the pattern is on the sheet.

**The smallest first step that is independently valuable: step 0, the encoder.** It is a pure function, it changes no
bytes on the wire, it needs no device, and it is verifiable by an equivalence test — and it is what turns the tick's
work from a number every other budget has to subtract into a number nobody has to think about.

**Step 0, written as a work order** (it is small enough to be one row):

| | |
| --- | --- |
| *What* | replace the per-byte `push_back` loop in `encode_cloud_snapshot` (`engine/include/openscanner/frame_protocol.h:39-73`) with a sized write of the same bytes; the envelope (`:59-66`) is unchanged |
| *Accept* | the encoded bytes are **identical** to the current encoder's, for every point count including 0 and 1, proven by a device-free equivalence test over random payloads; the tick's per-tick CPU (the `[perf]` line's segments) drops by the encoder's share — the 63 % of the tick's CPU measured in [#147 §7] |
| *Must not change* | the V1 wire format; the decoder in `ui/frame_protocol.js`; the tick's pacing (the wait at `main.cpp:1130`) |
| *Device* | none — the equivalence test and the `[perf]` line are both device-free |
| *Reversible* | yes |
| *Then* | re-run [#147]'s method (§2.4 there) to re-baseline the tick's CPU, so step 1's budget starts from the new number |

**The irreversible one: step 5, persistence (#149) — the archive's format and location.** Once scans are written in
a format, changing the format means migrating archives; once `output_dir` moves off `/tmp` ([#136 stage 8]), the
existing runs' artifacts are stranded. (A second, quieter one-way door: if colour is ever added to the frame — #136b
— the recorded archives and the UI have to move together, so that decision should not be smuggled in through a
seam.)

---

## 6. What must not change

* **The ABI** — `engine/include/openscanner/obscan_api.h`. Recon owns it; the builder never edits it. Every seam
  above is in our own code, so none of them needs an ABI change; if one ever does, it is a recon row.
* **The wire protocol** — the V1 envelope (`frame_protocol.h:39-73`) and what the UI decodes
  (`ui/frame_protocol.js`). The encoder change must be byte-identical; the colour question is #136b's, and it is a
  product decision, not a refactor.
* **The clean-room posture** — every seam named here is in `engine/` or `tools/`, i.e. code written from the header
  alone. None of them requires reading a vendor binary, and none moves the wall that the charter records
  (`docs/recomp/ARCHITECTURE.md`, "The walls").
* **The tick's measured pacing** — the wait at `main.cpp:1130` and the 100 ms budget stay as they are. The steps
  above shrink the *work inside* the tick; **the cadence itself is #113's decision**, and the 9.68 Hz ceiling
  ([#147 §5]) is a platform fact, not a bug to be optimised away. The **2.8× publish headroom** that changing the
  pace would unlock ([#151], §3) is #113's to take, not this note's to spend. C2 changes the *primitive* the wait uses
  (a deadline instead of a relative sleep) so that the rate *can* be requested — it does not request one, and it does
  not change the period: the requested period stays 100 ms until #113 says otherwise.

---

## 7. What this note does not decide

* **Colour** (#136b): it changes the frame size (12 B/point → 24 or 36 B/point) and therefore every budget in §3.
  It must be settled **before** S5, and it is a product decision, not a structural one.
* **The archive format and the disk budget** (#149): with the operator, in step 5.
* **Whether the 20,000-point live budget stays.** [#136]'s #136c says it should — the UI needs a bounded frame — and
  §3/A is consistent with that: "10× points" needs a **different stream**, not a bigger budget on the same one.
* **The cadence** (#113): what that row needs is the **2.8× publish headroom** (a 24.0 Hz source against 8.7 Hz
  published, [#151]), the **22.4 Hz** the pacing mechanism can reach when asked ([#147 §5]), and the fact that the
  9.68 Hz ceiling is the ceiling of our *current pace*, not of the hardware — together with #151's load test at
  10 / 24 / 50 Hz, whose own constraint is the platform's wait floor ([#147 §14]).
* **The per-stage budgets** (#152) and the end-to-end frame benchmark (#156), and the full stage map with costs
  (#155) — this note is the *structure* argument those rows instantiate; the numbers in §3 are the inputs, not the
  budgets. The four consumers' own compute budgets are new stages and belong to #152.
* **The lower-end machine.** §4.1 assumes half the M1's single-core throughput and half its memory bandwidth; the
  operator names the real target, and the factor moves with it. The *invariants* (§4.1) do not move with it — they
  are exact.
* **W5's price** (the every-tick exposure re-assert): **never measured, and now moot** — row **#153** landed while
  this note was being written and made the push conditional, so the waste is gone from the code. It stays on the
  sheet as the *pattern* to watch for elsewhere; what is still open is whether the exposure *segment* (#141's
  instrument) now reads ~0 on the next hardware run, which would confirm the fix end to end.
* **The tier's values and the probe that picks them** (#154): §4.2 specifies the tier's *shape* and where it rides;
  the numbers, the probe thresholds and the "a user may choose above their tier" rule are that row's and the
  operator's.
* **The colour axis** (#136b): whether the product carries colour at all. Until that is decided the tier's colour axis
  is a constant, not a knob, and the V1 frame carries none.
* **The page's accumulation cap**, and what the page does past it: the 75.9 MB accumulation is the one unbounded
  thing in the product today; the cap and the policy beyond it are the UI's and the operator's.
* **The low-tier floor's latency number.** "Very low latency" and "near real-time" are not yet a number: the
  acceptable **age of a frame at a logic consumer** is the input that #113 and the consumers' own rows need, and it is
  what makes the publish rate the *last* lever in the ladder rather than the first.
* **Who may demand the full cloud, and what the server says when it cannot**: a consumer's requirement is a
  capability request (§4.2), and the negotiation — the request, the server's answer, the memory it reserves — is not
  designed here.
* **Whether the ladder may step below a manual preset.** The probe is not a gate, so an operator's "High" should not be
  silently overridden — but a machine that cannot keep up must not overrun its budget either. The floor, the
  hysteresis and the recovery rule are product decisions (#152's to enforce, the operator's to set).
* **The probe's result's lifetime**: if it is cached, for how long, and what invalidates it. The note's position is
  the conservative one — a probe before the session, and a re-probe out of band — because a stale classification on a
  changed host is the failure mode a cache invites.
* **How the UI presents the presets and the override** (#154's deliverable (c)): the note fixes the *data* the UI
  needs (the classification, the presets, the active tier, and the reason for the last step) and not its shape.
* **Each consumer's data volume and identity model** — whether marker ID keys on the vendor's 0.1 voxel grid
  ([#136] stage 0) or on the raw cloud, and what resolution an ellipse transform is defined at, are product
  decisions with the operator; §4.1's table states what each one *needs*, not what it will get.
* **Borrowed view or shared copy** (C1): a consumer may read the SDK's borrowed buffer before the release
  (`main.cpp:1328`) — free, but bounded by the tick — or the ingest may copy what the consumers need (the 394 MB/s
  copy budget, L2). Which one is right depends on the consumers' latencies, which are not specified yet.
* **A device-level capture instant.** The OBScan ABI does expose timestamp getters — `obscan_frame_get_timestamp`
  (`obscan_api.h:924`) and `obscan_image_get_timestamp` (`:950`) — but on the **frame** and **image** handles, not on
  the model cloud the live tick publishes, and our loader resolves neither (`engine/src/obscan_loader.cpp`). So the
  *device's* capture time is not available on the path we use; C1's per-stage timestamps measure our pipeline's own
  latencies, which is what the operator asked to see.
