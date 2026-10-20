# The performance contract — budgets per stage, per preset, on a measured profile

*Row #152. The operator's standard: the ingest is the start of a long pipeline and must be **as fast as
possible** — extending Creality Scan's features and running on **lower-end hardware** depends on it. The
operator's own framing of why: *"we are 'lighter' at lighter loads for less CPU performance or memory
size/bandwidth."*

This file is the contract. `docs/recomp/perf-budgets.json` is the same budgets in machine-readable form,
and `scripts/perf-budget.py` is the harness that checks them. **Every number below names where it came
from**; nothing here is an impression.

---

## 1. The reference profile

### 1.1 The measured side — the M1, device-free

**#147's baseline** (`docs/recomp/log/2026-10-19-row-147-runtime-baseline.md`), on `origin/main` = `f20fc46`:
Apple M1, 8 cores / 17 GB, macOS 27.0.1, `sample` / `xctrace` / `vmmap` / `ps`.

| quantity | measured | where |
| --- | --- | --- |
| the synthetic tick's CPU | **195 µs** (0 clients) / **306 µs** (1) / **514 µs** (8) | #147 §3 |
| the encoder's share of the tick's CPU | **63 %** (67 ms of 106 ms) | #147 §7 |
| the broadcast | **35.9 – 38.4 µs per client per tick** at 8 clients | #147 §6 |
| the live allocation footprint | **86 – 87 KB, flat** | #147 §3.2 |
| the platform's wait floors | a 41 ms request realises **44.6 ms**; a 20 ms request **24.3 ms** (#147 §5) — the harness measures 41.9 – 45.9 ms, 20.3 – 24.7 ms and 16.1 – 19.8 ms (the 41, 20 and 16 ms requests) on the recorded runs |

### 1.2 The measured side — the live tick, on the device

**#141's live split** (the `[perf] summary` line of the 2026-10-20 run, 1,047 ticks; the record
`tests/results/2026-10-20-plan141-live-tick-split.md`). This is the *hardware* half of the profile, and
it is the half that changed the contract:

| segment | mean | max | what it is |
| --- | --- | --- | --- |
| `pose` | 0.00 ms | 0.06 ms | the pose read |
| `getter` | 0.00 ms | 0.07 ms | the publish getter — the vendor's fusion runs on its own threads |
| `post` | **0.45 ms** | 1.40 ms | **our own work**: the decimation + the encode |
| `bcast` | 0.00 ms | 0.05 ms | the broadcast (no client in that run) |
| `wait` | **102.99 ms** | 201.99 ms | the pacing wait — the cadence, not the ingest |
| `exposure` | **27.17 ms** | **597.64 ms** | **the every-tick exposure re-assert** |
| `release` | 0.00 ms | 0.02 ms | the cloud release |
| `residual` | 0.01 ms | | the tick's unaccounted work |

**The finding that reorganises this contract:** the ingest — everything in the tick except the pacing
wait — costs **27.62 ms** per consumed frame today, and **98 % of it is the exposure re-assert**, a
vendor call that re-asserts a value that has not changed (row #153 owns the fix). Our own work is
**0.45 ms**. The source is **24.0 Hz** (row #151), so today's ingest consumes **66 % of a core** at the
source's rate and **166 %** at 60 Hz: **the pipeline's rate ceiling is a waste**, not the machine.

### 1.3 The lower-end target — a *preset*, not a named machine

**The operator's answer to "what is the low-end floor?" is: don't declare one, measure it** (row #154).
So this contract has **no floor machine**. Instead:

* the **presets** below (High / Balanced / Lite / Capture-only) are the *intent*: a publish rate, a
  point budget, the colour axis, capture on/off and the browser's accumulation cap. Row #154 owns the
  **probe** that classifies a host into one, and the UI's override; this contract owns **the budgets
  each preset must meet** and the ladder that moves between them.
* the **measured M1 stays the reference point** the presets are calibrated against, and the per-preset
  budgets are stated in absolute units (µs, ms, MB/s, bytes), so a probe's classification can be
  checked against them.
* the **factor** (how much slower than the M1 a host is) is applied by the harness's `--factor N` when
  the numbers are checked; the default 1.0 checks the reference machine's budgets. The **lower-end
  target is not a named machine**, so no single factor is baked into the table — the table is per
  *preset*, and the *machine* decides which preset it can afford. That is the operator's own framing:
  the point of going faster is that a weaker host simply lands on a lower preset.

### 1.4 What the profile does *not* cover

* The **browser's render cost** — no device-free measurement exists. #142 (the HUD's frame age, DONE)
  measures the *age*; the render cost is #155's stage map. The browser's budgets here are the *volume*
  it must absorb and the memory it holds.
* The **live path's vendor calls** other than the two now timed (the exposure re-assert and the cloud
  release): the getter and the pose are 0.00 ms, so nothing is missing there.

---

## 2. The budgets, per stage and per preset

The machine-readable form is `docs/recomp/perf-budgets.json`; the harness reads that file, so the
contract and the check cannot drift apart. The **dataset** the numbers come from is the 8 runs in the
record's raw log (`--repeat 8`); `--sync-dataset` copies it into the budget file and `--check-dataset`
verifies the file, the contract and the log agree.

### 2.1 Ingest — per consumed source frame

| | M1 budget | derived from |
| --- | --- | --- |
| **the ingest, per consumed frame** | **≤ 1.5 ms** | the live `post` segment's measured max (**1.40 ms**, #141's split) with margin — this is what the ingest costs once the exposure re-assert is on change only (#153) |
| today | **27.62 ms** | the same split: 0.45 (post) + **27.17 (exposure)** + 0.00 (release) |
| the pacing wait | *not* the ingest | 102.99 ms mean — the cadence, and #113's question |

**The rates** — the budget's utilisation at each source rate, and today's:

| source rate | the frame period | the budget (≤ 1.5 ms) | today (27.62 ms) |
| --- | --- | --- | --- |
| **24 Hz** (the source's rate, #151) | 41.7 ms | **3.6 %** of a core | **66 %** |
| **50 Hz** | 20.0 ms | **7.5 %** | **138 %** — infeasible today |
| **60 Hz** | 16.7 ms | **9.0 %** | **166 %** — infeasible today |

So the ingest is not the limit at any rate **once the waste is gone**, and it is the limit at 50/60 Hz
**while it is there**. That is the whole of the operator's thesis in one table: the fastest thing to fix
is the one call that re-asserts an unchanged value.

### 2.2 The presets and their budgets

Each preset is a *shape* of the pipeline; the numbers are the M1's measured costs scaled by the
preset's knobs (the point budget scales the encode and the bytes; the publish rate scales the bandwidth
and the interval). The **machine** is what decides whether a host can afford a preset — the probe
(#154) classifies it, the ladder (§4) moves it down when a budget is missed.

| | **High** | **Balanced** | **Lite** | **Capture-only** |
| --- | --- | --- | --- | --- |
| the publish rate | 24 Hz | 24 Hz | 12 Hz | — (no publish) |
| the point budget | 20,000 | 12,000 | 5,000 | 0 |
| the colour axis | on | on | off | off |
| capture | on | on | on | on |
| the browser's accumulation cap | 76 MB | 40 MB | 15 MB | 0 |
| the network | the preview + the mesh at teardown | the preview | the preview | nothing |
| **the frame** | 240,036 B | 144,036 B | 60,036 B | 0 B |
| **CPU — the encode** | ≤ 820 µs | ≤ 490 µs | ≤ 205 µs | 0 |
| **CPU — the broadcast, per client** | ≤ 50 µs | ≤ 23 µs | ≤ 10 µs | 0 |
| **CPU — the tick's work, per frame** | ≤ 1.0 ms | ≤ 0.6 ms | ≤ 0.25 ms | 0 |
| **memory — the server's live footprint** | ≤ 128 KB | ≤ 128 KB | ≤ 128 KB | ≤ 128 KB |
| **memory — the browser's accumulation** | ≤ 76 MB | ≤ 40 MB | ≤ 15 MB | 0 |
| **bandwidth — per client** | ≤ 6.0 MB/s | ≤ 3.5 MB/s | ≤ 0.75 MB/s | 0 |
| **latency — the frame age** (capture → consumer) | ≤ 5.0 ms | ≤ 4.5 ms | ≤ 4.0 ms | n/a |
| **latency — the user-visible age** (interval + age) | ≤ 50 ms | ≤ 45 ms | ≤ 90 ms | n/a |

*Derivation.* The encode: the dataset's **531.5 – 553.3 µs** (median 538.6) at 20,000 points — and **61.5 – 63.3 µs** (median 62.1) at the synthetic path's 2,304 points — so ~27.2 ns/point; each
preset's budget is 1.5× its own scaled cost. The broadcast: the dataset's **17.6 – 27.8 µs per client**
(median 21.7) at 240,036 B, so ~0.105 ns/byte; the same scaling. The bandwidth: the frame's bytes × the publish rate
(the High preset's 5.76 MB/s is #136's figure at 24 Hz). The server's footprint: the measured
**100,352 B** against a 128 KB cap. The browser: #136's **75.9 MB** for the full-detail mesh, capped per
preset. The frame age: the tick's work plus the transfer; the device-free measurement of the age is
**0.194 ms p50** (§5.3), so a 5 ms budget is the measured value with a wide margin — the transfer half
is the kernel's, not ours.

### 2.3 Broadcast, per client

| metric | M1 budget | derived from | the harness measured |
| --- | --- | --- | --- |
| CPU per client per frame | **≤ 50 µs** | the dataset's 17.6 – 27.8 µs (median 21.7; 1.98×) | 17.6 – 27.8 µs |
| CPU per frame, 8 clients | **≤ 400 µs** | the dataset's 141.1 – 222.6 µs (median 173.8; 1.98×) | 141.1 – 222.6 µs |
| syscalls per client per frame | **2 today** (`ws_broadcast_server.cpp:255` sends the 2 – 4 B header, `:256` the payload) → **1** is the target | the source | 2 |
| the stall risk | **≤ 100 ms per client** — the `SO_SNDTIMEO` at `ws_broadcast_server.cpp:304-307`, the fan-out is **synchronous and holds `clients_mutex`** for the whole fan-out (`:503-517`), no queue, no drop policy | the source | — |

**The stall risk is the budget that is not a number.** One stalled client can hold the tick for up to
100 ms: at the source's 24 Hz that is 2.4 frame periods, at 50 Hz five. The per-client cost is small; the
*shape* is the risk (#113/#148's S3).

### 2.4 Browser consumption

| metric | budget | derived from |
| --- | --- | --- |
| the stream per client | **2.09 MB/s** at 8.7 Hz (today), **5.76 MB/s** at 24 Hz, **12.0 MB/s** at 50 Hz, **14.4 MB/s** at 60 Hz | 240,036 B × the rate (#136's figure at 8.7 Hz) |
| the accumulation | **≤ 76 MB** for the full-detail mesh in the page's `Float32Array` | #136 §7: 6,323,235 vertices × 12 B = 75.9 MB, memory-only (nothing persisted) — the one *unbounded* thing in the product, and the cap is what makes it a tier item |
| the render cost | **not measured** — #155's stage map | — |

### 2.5 Memory

| metric | budget | derived from | the harness measured |
| --- | --- | --- | --- |
| the server's live allocation footprint | **≤ 128 KB, and flat** (no growth per tick) | #147 §3.2: 86 – 87 KB over 30 s, +7/8 allocations (the accept path, not a per-tick accumulation) | 100,352 B |
| the browser's page memory | **≤ 76 MB** | #136 §7 | — (no browser in the harness) |
| the `--record` path's cap | 1 GiB (~8.6 min) | #136 | — |

---

## 3. The waste inventory — every item priced

Each item: what it is, where it is, what it costs, and what it would cost to remove. The prices are
from #147's profile, #141's live split and the source; the ones marked *(extrapolated)* are derived, and
say so.

| # | the waste | where | the price (measured) | the price in the pipeline |
| --- | --- | --- | --- | --- |
| **1** | **the every-tick exposure re-assert** — `obscan_scan_config_combined_exposure_and_laser_current` is called on every tick although the value rarely changes | `tools/scan_server/main.cpp:1148-1151` (the value is set at `:1038`) | **27.17 ms mean, 597.64 ms max** per tick (#141's live split, 1,047 ticks) | **98 % of the ingest** — 27.62 ms of the 28 ms a frame costs, against 0.45 ms of our own work. At the source's 24 Hz it is **66 % of a core**; at 60 Hz, 166 %. #153 owns the fix (a dirty flag); #141's live run measures the saving |
| **2** | **the per-byte `push_back` encoder** — `put_f32` writes four bytes with four `push_back`s, so a frame is 27,684 bounds-checked pushes | `engine/include/openscanner/frame_protocol.h:39-73` | **63 % of the tick's CPU with no client** (67 ms of 106 ms in a 20 s Time Profiler run) | 123 µs of the 195 µs tick → 3.34 ms of CPU per second at 27.2 ticks/s. On the live path the encoder is inside the 0.45 ms `post` segment, and 20,000 points × 12 B is 240 kB written one byte at a time |
| **3** | **the WebSocket send** — the header and the payload are two `send`s per client per frame | `engine/src/ws_broadcast_server.cpp:255-256` | **57.7 % of the tick's CPU at 8 clients** (153 ms of 265 ms) | 297 µs of the 514 µs tick → 8.1 ms of CPU per second; 435 syscalls/s at 8 clients. One `send` with the header in front halves it |
| **4** | **the two-vector copy** — `std::vector<Vec3> frame_points = base_points;` copies the whole cloud and then rotates it | `tools/scan_server/main.cpp:603-604` | ~**4 % of the tick's CPU** (the copy + `RotateY`) | 7.8 µs of the 195 µs tick → 0.21 ms/s; **752 KB/s** of copy bandwidth at 27.2 ticks/s. **Synthetic-path only** — the live path decimates the vendor's buffer in place and does not copy it |
| **5** | **the per-tick allocations** — two blocks per tick: the copy (27,648 B) and the encode buffer (27,684 B) | `tools/scan_server/main.cpp:603`, `frame_protocol.h:42` | the allocator is **~2 % of the tick's CPU**; **~55 allocations/s and ~1.5 MB/s of churn** with no client, ~270 allocations/s with eight | 3.9 µs per tick → 0.1 ms/s; the *live* set is flat (86 – 87 KB), so this is churn, not a leak |
| **6** | **the msgpack byte-at-a-time parse** — `Reader::bin` copies the band one `next()` call at a time | `tools/raptor_client/main.cpp` (`Reader::bin`) | **50 % of the replay client's CPU** (782 ms of 1,561 ms) | 30 µs per frame (the unpaced replay) → 0.72 ms/s at 24 Hz. A `memcpy` out of the read buffer halves it |
| **7** | **the broadcast's shape** — a synchronous fan-out under `clients_mutex`, with a 100 ms `SO_SNDTIMEO` per client and no queue | `engine/src/ws_broadcast_server.cpp:503-517`, `:304-307` | the *cost* is per-client and small (§2.3); the *risk* is the stall | one stalled client can hold the tick for **100 ms** — 2.4 frame periods at 24 Hz, 5 at 50 Hz |

**The three biggest, in order:** the **exposure re-assert** (27.17 ms per tick — 60× our own work, and
98 % of the ingest), the **encoder** (63 % of the tick's CPU, on every frame, at every client count), and
the **broadcast's send** (57.7 % at 8 clients, linear in the client count). The first is a *waste* (the
value has not changed); the second and third are *work* that the seams S1/S2 in #148's note would make
cheaper.

---

## 4. The degradation ladder

The ladder sheds **quality, not frames** (#148's order, which this contract enforces):

| step | the knob | what it costs in quality | what it saves |
| --- | --- | --- | --- |
| 1 | **the point budget** (20,000 → 12,000 → 5,000) | the preview's density | the encode and the bytes, proportionally (27.2 ns/point) — the same code with a smaller number |
| 2 | **the colour axis** (when it exists) | the colours | the bytes per point |
| 3 | **the full-detail mesh stream** at teardown | the high-resolution mesh in the page | the page's memory falls from 75.9 MB to the preview |
| 4 | **the publish rate** (24 → 12 Hz) | **the latency the pipeline exists for** | the cost per second |
| 5 | **the client count** | the extra viewers | the fan-out |

It **cannot** touch the ingest (0.45 ms of our own work, and the vendor's getter at 0.00 ms), the pose
stream (160 B/tick — what tracking consumes), the capture, or the pacing.

**The rules.**

* **Step down.** A *miss* is a stage over its budget, detectable today by #141's instrument. A
  **sustained** miss — over budget for **1 second** of ticks — steps the pipeline down **one** preset,
  records the step, and makes it visible in the status message (so the UI can say why the quality
  changed). One second, not one tick: a single slow tick is the machine, not the pipeline.
* **Step up (the recovery rule).** A step back up happens only when **all** of these hold: the stage has
  been **within its budget for 10 seconds**; its measured value is at **≤ 50 % of the budget** (not
  merely back under it); and **30 seconds** have passed since the last step. That is the hysteresis: a
  pipeline that is merely at the budget does not climb, so a borderline host does not oscillate.
* **The ceiling.** The step-up never goes above the preset **the operator chose**. The probe is not a
  gate (#154: a user may choose above their tier), so the ladder may **step below** a manual choice —
  an overrunning budget is worse than a lower quality — but it never climbs above it.
* **The floor.** The ladder never steps below **Capture-only**: the ingest continues, the capture is
  kept, and the scan is not lost. That is the floor the operator named.
* **One preset per step.** The ladder moves between presets, not between continuous values (#148), so
  every step is a whole, describable configuration.

---

## 5. The harness

```
python3 scripts/perf-budget.py                 # the per-stage budgets (~13 s)
python3 scripts/perf-budget.py --repeat 8      # the dataset the budgets are derived from
python3 scripts/perf-budget.py --full-frame    # the full-frame mode (row #156)
python3 scripts/perf-budget.py --check-dataset # the budget file, the contract and the log agree
python3 scripts/perf-budget.py --factor 3      # as if this machine were 3x slower
```

* **Device-free.** It runs the synthetic `scan_server` (no vendor SDK, no device) and the
  `perf_budget` binary (the product's own encoder and broadcast, in process). It never touches a
  device, and it never opens a port outside loopback except the server's own listener.
* **Bounded.** The benchmark's phases have fixed iteration counts; the server runs for a fixed number
  of seconds and is then stopped (`SIGTERM`, then `kill` if it does not go). The whole run is ~13 s.
* **It fails on a regression.** Each budget in `docs/recomp/perf-budgets.json` is compared with the
  measurement; the exit code is **1** if a measured budget is exceeded, **0** if every one is met, and
  **2** if the harness could not run. The lower-end rows are a **projection**, not a gate: they are
  reported as `OVER (target)` when the assumed target cannot sustain the rate, and the run still exits
  0 — that is a finding for the operator, not a regression of the code.
* **It is not a `ctest` entry, on purpose.** A performance budget on a shared CI machine fails for
  reasons that are not regressions (the loader, the neighbours), and a red build for that would train
  everyone to ignore it. It is run **on demand**, on a quiet machine, and its result is recorded.

### 5.1 What it does not check

The live path's vendor calls (no device — but the exposure re-assert is now *measured* on hardware and
budgeted from that), the browser's render cost (no browser), and the memory of anything but the
synthetic server.

### 5.2 The budgets' derivation, and the rule that keeps them honest

Every CPU budget is **K × the median of the recorded dataset**, and K is stated per row: **1.52 / 1.53**
(the encoder), **2.30** (the broadcast), **1.10 – 1.27** (the platform's wait floors — that number is
the machine's timer), **1.31** (the footprint). A **K× regression trips it** and a **K× contention does
not**. The tick's post segment has **no K**: the dataset holds one sample and the value ranged
one sample in the recorded dataset (0.090 ms), so its budget gates no specific regression size and
**cannot gate a doubling** — the contract says so, and the encoder's and the broadcast's budgets carry that gate.

**The numbers are derived, not typed.** `--sync-dataset` copies the record's raw log's summary block
into the budget file, recomputes each K and fills each derivation's dataset token from the same place;
`--check-dataset` verifies the budget file, the contract and the log agree and **fails if they do
not**.

### 5.3 The full-frame mode (row #156)

One frame through **every** stage, timed end to end: the synthetic `scan_server` encodes and
broadcasts it, and a consumer probe receives it and measures the **age** — the time from the frame's
capture stamp to the moment the consumer has it. The per-stage split comes from the server's own
`[perf] window` line, so **the stages and the total are the same run**:

```
stage                             value     unit  where it comes from
encode + post                     0.070       ms  the server's [perf] window: post p50
broadcast                         0.030       ms  the server's [perf] window: bcast p50
transfer + consume                0.099       ms  the age minus the two stages above
TOTAL (the frame age)             0.199       ms  the consumer's measurement: p50
  the age at p95                  0.377       ms  the consumer's measurement
  the age at max                  0.599       ms  the consumer's measurement
```

The **total is the regression threshold** (the preset's `e2e_frame_age_ms`): the harness fails when it
regresses. The mode is device-free, bounded and runnable on demand and in CI. The **preset's knobs**
(the point budget and the publish rate) need the server's `--synthetic-load` mode (#151, PR #366); until
it lands, the mode measures the plain synthetic path (2,304 points at 30 Hz) and **says so** in its
report rather than pretending the knobs were set. The live split — the hardware variant — is #141's and
#142's, and the frame age they report is the same number this mode measures, on the device.

---

## 6. Open questions

1. **The exposure re-assert's saving is not yet measured.** #153 owns the fix (a dirty flag) and #141's
   next live run measures the `exposure` segment after it. The contract budgets the ingest as if the
   fix were in (≤ 1.5 ms) and states today's cost beside it (27.62 ms); the saving is the size of the
   gap, and it is the largest single win in the pipeline.
2. **The probe is #154's, and the presets' *thresholds* are its.** This contract states what each
   preset must meet; which host lands in which preset is the probe's decision, and the user may choose
   above their tier.
3. **The browser's render cost is unmeasured.** The volumes and the page's memory are budgeted; the
   render and the drop rate are #155's (and #151's browser side).
4. **The frame age's transfer half is the kernel's.** The device-free measurement (0.199 ms p50) is on a
   loopback; the real network and the real browser add to it, and #142's frame age is the number that
   closes that gap.
5. **The ladder's step-down threshold (1 s of misses) and its step-up hysteresis (10 s within budget at
   ≤ 50 %, 30 s between steps) are stated rules, not measurements.** They are the kind of number that
   wants a field run; the contract names them so they can be argued with.
6. **The platform's timer is the ceiling at 50/60 Hz**, not the code (§2.1's footnote and #148): a
   41 ms request realises 44.6 ms, so the source's 24 Hz is 22.4 Hz of headroom on this platform
   whatever the pipeline costs.
