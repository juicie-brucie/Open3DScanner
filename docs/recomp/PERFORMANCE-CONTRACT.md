# The performance contract — budgets per stage, on a reference profile

*Row #152. The operator's standard: the ingest is the start of a long pipeline and must be **as fast as
possible** — extending Creality Scan's features and running on **lower-end hardware** depends on it.*

This file is the contract. `docs/recomp/perf-budgets.json` is the same budgets in machine-readable form,
and `scripts/perf-budget.py` is the harness that checks them. **Every number below names where it came
from**; nothing here is an impression.

---

## 1. The reference profile

### 1.1 The measured side — the M1

The measured profile is **#147's device-free baseline**
(`docs/recomp/log/2026-10-19-row-147-runtime-baseline.md`), taken on `origin/main` = `f20fc46`:

| | |
| --- | --- |
| Machine | Apple M1, 8 cores / 17 GB, macOS 27.0.1 (26A434) arm64 |
| Method | `sample`, `xctrace` (Time Profiler / Allocations), `vmmap`, `ps`; the harness and its four scripts are reproduced in #147 §11 |
| Surfaces | the `scan_server` tick loop (synthetic), the `raptor_client` replay path, the WebSocket broadcast |
| Load during the runs | 2.0 – 7.2 (other rooms building); medians stable to ±5 %, maxima not |
| Not measured | the live path's vendor calls — they need a device (#141's live run has **not** landed; its instrument has, but the run is still `TODO`/`HW`) |

The headline numbers of that profile, restated because the budgets are derived from them:

| quantity | measured | where |
| --- | --- | --- |
| the synthetic tick's period | 36.9 – 37.5 ms p50 (the nominal 30 Hz is unreachable) | #147 §3 |
| the tick's CPU | **195 µs** (0 clients) / **306 µs** (1) / **514 µs** (8) | #147 §3 |
| the tick's CPU vs wall | 195 µs in a 37.0 ms tick = **0.53 %** — the tick is 99.5 % waiting | #147 §3 |
| the encoder's share of the tick's CPU | **63 %** (67 ms of 106 ms, no client) | #147 §7 |
| the broadcast's share at 8 clients | **57.7 %** (153 ms of 265 ms) | #147 §7 |
| the broadcast's cost | **35.9 – 38.4 µs per client per tick** at 8 clients | #147 §6 |
| the platform's wait floor (41 ms request) | **44.62 ms** → **22.4 Hz** | #147 §5, §14 |
| the platform's wait floor (20 ms request) | **24.29 ms** → **41.2 Hz** | #147 §5, §14 |
| the live tick's wait floor (100 ms run-loop) | **103.19 – 103.75 ms** → **9.68 Hz** | #147 §5 |
| the live allocation footprint | **86 – 87 KB, flat** (+7/8 allocations over 30 s) | #147 §3.2 |
| the live path's post segment | **1.18 – 1.68 ms** p50 (20,000 points) | #112 (hardware) |
| the live V1 frame | **240,036 B**, 2.09 MB/s at 8.7 Hz | #136 |
| the browser's accumulated mesh | **75.9 MB** (6,323,235 × 12 B) | #136 §7 |
| the source rate actually seen | **≈24.0 Hz** (8,119 device frames / 338.6 s) | #151's note |
| our publish rate | **8.7 Hz** (our own pacing: `kLiveTickIntervalMs = 100`) | #151's note |

### 1.2 The lower-end target — **an assumption, not a measurement**

**The operator has not named the target machine.** Until they do, this contract uses a **stated
assumption**:

> **a 4-core Intel machine, ~3× slower than the M1 on this workload** — the **factor is 3.0**.

Every CPU-time budget below is given **twice**: the M1's budget and the lower-end target's
(= M1 × 3.0). Two rules keep the arithmetic honest:

* **CPU-time budgets scale by the factor.** The work is the same work; the machine is slower.
* **Data-volume budgets do not.** Bytes per frame and MB/s are the wire volume; they are the same on
  any machine. What a slower machine changes is the *rate* it can push them at, which the factor
  does not capture — a lower-end machine's memory bandwidth is a separate question, and it is the
  browser's and the socket's problem, not the encoder's.

**Re-basing when the target is named** is one edit: change `reference.lower_end_factor` in
`docs/recomp/perf-budgets.json` and re-run the harness with `--factor N`. Nothing else in the
contract changes shape.

### 1.3 What the profile does *not* cover

* The **live path's vendor calls** (the publish getter, the exposure re-assert, the cloud release) —
  device-only. #141's instrument now times them (`exposure` and `release` segments), but **its live
  run has not landed**: row #141 is `TODO` on main. The ingest budget therefore carries an
  **allowance** for them, not a measurement (§2.1).
* The **browser** — no device-free measurement of the render cost exists. #142 (the HUD's frame age)
  and #151 (the publish-rate load test, browser side) own it. The browser budgets in §2.4 are the
  *volume* the page must absorb, plus the memory it holds; the render cost is an open question (§5).

---

## 2. The budgets, per stage

The machine-readable form is `docs/recomp/perf-budgets.json`; the harness reads that file, so the
contract and the check cannot drift apart. The harness's own measurements are listed beside the
budgets — they are what it measured on this run of `origin/main`, not the source of the budget.

### 2.1 Ingest — per consumed source frame, at 24 / 50 / 60 Hz

The ingest budget is **per consumed frame**: our post-processing + encode, plus the two untimed
vendor calls' allowance.

| part | M1 budget | lower-end budget | derived from |
| --- | --- | --- | --- |
| our post-processing + encode | **≤ 2.0 ms** | ≤ 6.0 ms | the live `post` segment measured 1.18 – 1.68 ms p50 in #112 (decode + encode + decimation) |
| the vendor calls' allowance | **≤ 6.0 ms** | ≤ 18.0 ms | #134's 9.6 ms unsegmented residual, less the platform's own 3.2 – 3.9 ms wait overshoot (#147 §9) → ~5.6 – 6.4 ms of untimed vendor work; #153 names the exposure re-assert as one of the two |
| **ingest, per consumed frame** | **≤ 8.0 ms** | **≤ 24.0 ms** | the two above |

The synthetic tick's own post segment (the device-free proxy) has its own budget: **≤ 0.40 ms**
(M1) / ≤ 1.20 ms (lower-end), from #147 §3's measured 0.12 – 0.20 ms p50.

**The rates.** The platform's own wait floor sets the ceiling, and it is measured by the harness
every run (zero work in the loop, the same control as #147 §5):

| source rate | the request it implies | the platform's floor | the ceiling that follows | M1 utilisation (at the nominal rate) | lower-end utilisation |
| --- | --- | --- | --- | --- | --- |
| **24 Hz** | 41 ms | 44.6 – 46.0 ms | **≈21.7 – 22.5 Hz** | **19.2 %** of a core | **57.6 %** |
| **50 Hz** | 20 ms | 21.7 – 25.0 ms | **≈40 – 46 Hz** | **40.0 %** | **120 %** |
| **60 Hz** | 16 ms | 18.6 – 20.0 ms | **≈50 – 60 Hz** | **48.0 %** | **144 %** |

(The harness prints the utilisation twice — at the nominal rate and at the measured ceiling — because
the two answer different questions: "does the code fit in the frame?" and "can this machine deliver
the frame at all?".)

Three things follow, and they are the contract's findings:

1. **24 Hz is comfortable** — 19 % of a core on the M1, 58 % on the assumed target.
2. **50 Hz is the edge**: on the M1 the code is 40 % of a core, but on the assumed lower-end target it
   is **120 %** — the target cannot sustain 50 Hz at the current ingest cost.
3. **60 Hz is out of reach on the assumed target** (144 % of a core), and on this platform 60 Hz is
   at the edge of what the timer allows: the 16 ms request realises 18.6 – 20.0 ms, so the ceiling is
   ~50 – 60 Hz and moves with the machine's load.

**The binding limit at 50/60 Hz is not our code** — it is the platform's timer and, on the assumed
target, the total ingest cost. #147 measured the whole tick at 195 µs of CPU (0 clients) and 514 µs
at 8 clients: 0.5 % and 1.4 % of a 37 ms tick. The tick's own work is not what would stop a faster
publish rate.

### 2.2 Encode

| metric | M1 budget | lower-end budget | derived from | the harness measured |
| --- | --- | --- | --- | --- |
| bytes per frame (the live 20,000-point frame) | **240,036 B** (exact) | the same — the wire volume is machine-independent | #136's live V1 frame | 240,036 B |
| CPU per frame, 20,000 points | **≤ 1,200 µs** | ≤ 3,600 µs | the live `post` segment (1.18 – 1.68 ms, #112) covers decode + encode; the encoder alone must stay under 1.2 ms | 543 – 584 µs |
| CPU per frame, 2,304 points (the synthetic sphere) | **≤ 300 µs** | ≤ 900 µs | #147 §7: the encoder is 63 % of the 195 µs tick ≈ 123 µs | 62 – 68 µs |

### 2.3 Broadcast, per client

| metric | M1 budget | lower-end budget | derived from | the harness measured |
| --- | --- | --- | --- | --- |
| CPU per client per frame | **≤ 60 µs** | ≤ 180 µs | #147 §6: 35.9 – 38.4 µs per client per tick at 8 clients (the tick's own condition, where the client drains in the background) | 18 – 23 µs (the harness's client has a 4 MB receive buffer, so the send does not wait for the drain) |
| CPU per frame, 8 clients (the live frame) | **≤ 400 µs** | ≤ 1,200 µs | #147 §6's 0.29 – 0.31 ms per tick for the 27,697 B synthetic frame; this budget is for the 240,036 B live frame | 146 – 185 µs |
| syscalls per client per frame | **2 today** (`ws_broadcast_server.cpp:255` sends the 2 – 4 B header, `:256` the payload) → **1** is the target | | the source | 2 |
| the stall risk | **≤ 100 ms per client** — the `SO_SNDTIMEO` at `ws_broadcast_server.cpp:304-307`, the fan-out is **synchronous and holds `clients_mutex`** for the whole fan-out (`:503-517`), there is no queue and no drop policy | | the source | — |

**The stall risk is the budget that is not a number.** One stalled client can hold the tick for up to
100 ms. At the current 8.7 Hz publish that is 87 % of a period; at 24 Hz (41.7 ms) it is **2.4
periods**; at 50 Hz (20 ms) it is **5 periods**. The per-client cost is small; the *shape* is the
risk. #113/#148 own the structural question; this contract prices the exposure.

### 2.4 Browser consumption

| metric | budget | derived from |
| --- | --- | --- |
| the stream per client | **2.09 MB/s** at 8.7 Hz (today), **5.76 MB/s** at 24 Hz, **12.0 MB/s** at 50 Hz, **14.4 MB/s** at 60 Hz | 240,036 B × the rate (#136's figure at 8.7 Hz) |
| the accumulation | **≤ 76 MB** for the full-detail mesh in the page's `Float32Array` | #136 §7: 6,323,235 vertices × 12 B = 75.9 MB, memory-only (nothing persisted) |
| the render cost | **not measured** — the budget is a placeholder: zero dropped frames and a frame age ≤ 1 publish period | #142 (the HUD's frame age) and #151 (the browser side of the load test) own the measurement |

### 2.5 Memory

| metric | budget | derived from | the harness measured |
| --- | --- | --- | --- |
| the server's live allocation footprint | **≤ 128 KB, and flat** (no growth per tick) | #147 §3.2: 86 – 87 KB over 30 s, +7/8 allocations (the accept path, not a per-tick accumulation) | 100,352 B |
| the browser's page memory | **≤ 76 MB** | #136 §7 | — (no browser in the harness) |
| the `--record` path's cap | 1 GiB (~8.6 min) | #136 | — |

---

## 3. The waste inventory — every item priced

Each item: what it is, where it is, what it costs, and what it would cost to remove. The prices are
from #147's profile and the source; the ones marked *(extrapolated)* are derived, and say so.

| # | the waste | where | the price (measured) | the price in the pipeline |
| --- | --- | --- | --- | --- |
| **1** | **the per-byte `push_back` encoder** — `put_f32` writes four bytes with four `push_back`s, so a frame is 27,684 bounds-checked pushes | `engine/include/openscanner/frame_protocol.h:39-73` | **63 % of the tick's CPU with no client** (67 ms of 106 ms in a 20 s Time Profiler run; 18 of ~26 our-code `sample` hits at `main.cpp:607`) | 123 µs of the 195 µs tick → 3.34 ms of CPU per second at 27.2 ticks/s (**0.33 % of a core**). On the live path the encoder's share of the 1.18 – 1.68 ms post segment is ≈ 0.6 – 1.1 ms/frame *(extrapolated from the 63 % share)* → **14 – 26 ms/s at 24 Hz (1.4 – 2.6 % of a core)**; ×3 on the assumed target: 4.2 – 7.8 % |
| **2** | **the WebSocket send** — the header and the payload are two `send`s per client per frame | `engine/src/ws_broadcast_server.cpp:255-256` | **57.7 % of the tick's CPU at 8 clients** (153 ms of 265 ms) | 297 µs of the 514 µs tick → **8.1 ms of CPU per second at 27.2 ticks/s**; the syscalls are 2 × 8 × 27.2 = **435 per second** at 8 clients, 54/s at one client. One `send` with the header in front halves it |
| **3** | **the every-tick exposure re-assert** — `obscan_scan_config_combined_exposure_and_laser_current` is called on every tick although the value rarely changes | `tools/scan_server/main.cpp:1148-1151` (the value is set at `:1038`) | one of the two untimed vendor calls that make up **most of the live tick's ~5 – 6 ms unsegmented work** (#153) | ≤ 6 ms per tick → **52 ms of CPU per second at 8.7 Hz (5.2 % of a core)**; at 24 Hz, 144 ms/s (14.4 %); ×3 on the assumed target: 15.7 % at 8.7 Hz, 43 % at 24 Hz. #153 owns the fix (a dirty flag); the exact split is #141's live run |
| **4** | **the two-vector copy** — `std::vector<Vec3> frame_points = base_points;` copies the whole cloud and then rotates it | `tools/scan_server/main.cpp:603-604` | ~**4 % of the tick's CPU** (the copy + `RotateY`) | 7.8 µs of the 195 µs tick → 0.21 ms/s; **752 KB/s** of copy bandwidth at 27.2 ticks/s (27,648 B per tick). **Synthetic-path only** — the live path decimates the vendor's buffer in place and does not copy it |
| **5** | **the per-tick allocations** — two blocks per tick: the copy (27,648 B) and the encode buffer (27,684 B) | `tools/scan_server/main.cpp:603`, `frame_protocol.h:42` | the allocator is **~2 % of the tick's CPU**; **~55 allocations/s and ~1.5 MB/s of churn** with no client, ~270 allocations/s with eight | 3.9 µs per tick → 0.1 ms/s; the *live* set is flat (86 – 87 KB), so this is churn, not a leak |
| **6** | **the msgpack byte-at-a-time parse** — `Reader::bin` copies the band one `next()` call at a time | `tools/raptor_client/main.cpp` (`Reader::bin`) | **50 % of the replay client's CPU** (782 ms of 1,561 ms) | 30 µs per frame (the unpaced replay) → 0.72 ms/s at 24 Hz. A `memcpy` out of the read buffer halves it |
| **7** | **the broadcast's shape** — a synchronous fan-out under `clients_mutex`, with a 100 ms `SO_SNDTIMEO` per client and no queue | `engine/src/ws_broadcast_server.cpp:503-517`, `:304-307` | the *cost* is per-client and small (§2.3); the *risk* is the stall | one stalled client can hold the tick for **100 ms** — 2.4 publish periods at 24 Hz, 5 at 50 Hz |

**The three biggest, in order:** the **encoder** (63 % of the tick's CPU, on every frame, at every
client count), the **broadcast's send** (57.7 % at 8 clients, linear in the client count), and the
**exposure re-assert** (up to ~6 ms per tick on the live path, 5.2 % of a core at the current publish
rate and 43 % on the assumed target at 24 Hz).

---

## 4. The harness

```
python3 scripts/perf-budget.py                 # the full run (~13 s)
python3 scripts/perf-budget.py --quick         # a smoke run (numbers only, no verdicts)
python3 scripts/perf-budget.py --factor 3      # check the budgets as if this machine were 3x slower
```

* **Device-free.** It runs the synthetic `scan_server` (no vendor SDK) and the `perf_budget` binary
  (the product's own encoder and broadcast, in process). It never touches a device, and it never
  opens a port outside loopback except the server's own listener.
* **Bounded.** The benchmark's phases have fixed iteration counts; the server runs for a fixed
  number of seconds and is then stopped (`SIGTERM`, then `kill` if it does not go). The whole run is
  ~13 s.
* **It fails on a regression.** Each budget in `docs/recomp/perf-budgets.json` is compared with the
  measurement; the exit code is **1** if a measured budget is exceeded, **0** if every one is met,
  and **2** if the harness could not run.
* **It is not a `ctest` entry, on purpose.** A performance budget on a shared CI machine fails for
  reasons that are not regressions (the loader, the neighbours), and a red build for that would train
  everyone to ignore it. It is run **on demand**, on a quiet machine, and its result is recorded —
  which is what the record in `tests/results/` is. The lower-end rows are a **projection**, not a gate: they
  are reported as `OVER (target)` when the assumed target cannot sustain the rate, and the run still
  exits 0 — that is a finding for the operator, not a regression of the code.
* **It is deterministic in shape, not in digits.** The machine is shared (load 2.0 – 7.2 during
  #147's runs), so the budgets carry headroom (the encoder's budget is ~2 – 4× the measured cost).
  Two measurements needed care to be stable:
  * the **broadcast** must not wait for the client's drain, or the harness times the drain instead
    of the copy — the harness's client therefore asks for a **4 MB receive buffer** (the trick
    #147's harness client used), and the first ten broadcasts are discarded as warm-up. With
    that, the per-client cost holds at **18 – 23 µs** across runs; without it, it ranged 23 – 71 µs.
  * the **platform's wait floor** is the noisiest number of all: it moved 3.6 – 4.3 ms across
    #147's runs and 3.1 – 5.0 ms across the harness's. The budgets for it carry ~10 % of headroom.
  The harness also prints the **minimum** of each broadcast sample next to the median: on a shared
  machine the minimum is the uncontended cost, and it is the stable statistic.

**What it does not check:** the live path's vendor calls (no device), the browser's render cost (no
browser), and the memory of anything but the synthetic server.

---

## 5. Open questions

1. **The lower-end target is an assumption.** The operator has not named the machine; the 3.0×
   factor is a placeholder. Naming it re-bases the contract in one edit (§1.2).
2. **The vendor calls' allowance is an allowance, not a measurement.** #141's instrument times the
   exposure and release segments, but **its live run has not landed**; until it does, the 6.0 ms is
   derived from #134's 9.6 ms residual minus the platform's wait overshoot.
3. **The browser's render cost is unmeasured.** The volumes are budgeted (§2.4); the render and drop
   rate are #142's and #151's.
4. **The tick's own work is not the limit at 50/60 Hz** — the platform's timer is (§2.1). Whether to
   change the cadence is #113's question; this contract only states the ceiling the code must fit
   inside.
5. **The stall risk has no budget that can be "met"** — it is a shape, not a number (§2.3). The
   latest-frame-slot + per-client-writer design (#113/#148) is the structural answer.
