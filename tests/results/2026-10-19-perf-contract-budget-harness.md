# PLAN #152 — the performance contract (per preset), the degradation ladder, and the benchmark — PASS (device-free)

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-build4
- Branch: build/plan152-perf-contract
- Target: build/tools/scan_server/perf_budget (new, build-tree copy) + build/tools/scan_server/scan_server
- Other copy: none
- Preflight/postflight: **not run** — no vendor SDK is linked by either binary (`perf_budget` links
  `openscanner_engine` only; the synthetic `scan_server` path has no device), and the harness never
  contacts a device. No interposer and no crash-report sweep apply.
- Interposer: n/a — exempt, as for the #117, #120, #122 and #127 records.
- Run start: 2026-10-20 (the raw log carries the exact command sequence)
- Device: **not used**. The daemon on 10013/10010 was never contacted.

## What this PR delivers

Row #152, with the three addenda: the **reference profile** (the measured M1 baseline and the
**presets** the capability probe classifies a host into), the **per-preset budgets** (CPU, memory,
bandwidth, latency), the **degradation ladder** with its step-down and step-up rules, the **waste
inventory**, and the **regression benchmark**. The third addendum's **full-frame mode** (row #156's
deliverable) is in the same harness: one frame through every stage, timed end to end, with a
regression threshold on the total.

**No product behaviour changed.** The diff adds a benchmark target, a script, a contract and a
machine-readable budget file; no line of the product's own code was touched.

## Commands

```
# the per-stage budgets, the dataset, the full-frame mode, the checks
python3 scripts/perf-budget.py                 # the per-stage budgets (~13 s)
python3 scripts/perf-budget.py --repeat 8      # the dataset the budgets are derived from
python3 scripts/perf-budget.py --full-frame    # the full-frame mode (row #156)
python3 scripts/perf-budget.py --check-dataset # the budget file, the contract and the log agree
python3 scripts/perf-budget.py --factor 3      # as if this machine were 3x slower
```

It runs the synthetic `scan_server` (no vendor SDK, no device) and the `perf_budget` binary (the
product's own encoder and broadcast, in process).

## Result

- Exit code: **0** for every mode (the per-stage run, the 8-run dataset, the full-frame mode, the
  quick run, the `--factor 3` run and the dataset check)
- Verdict: **PASS** (device-free)
- Raw log: `2026-10-19-perf-contract-budget-harness.raw.log` (same directory) — it holds the **8-run
  dataset** the budgets are derived from, the full-frame run, and the checks

## The correction that reorganised the contract: the live tick's split

**#141's live split** (the `[perf] summary` line of the 2026-10-20 hardware run, 1,047 ticks; PR
#376's record, `tests/results/2026-10-20-plan141-live-tick-split.md`) is the *hardware* half of the
profile, and it changes the contract:

| segment | mean | max |
| --- | --- | --- |
| `pose` / `getter` / `release` | 0.00 ms | 0.07 ms |
| `post` (our own work: the decimation + the encode) | **0.45 ms** | 1.40 ms |
| `wait` (the pacing — the cadence, not the ingest) | 102.99 ms | 201.99 ms |
| **`exposure` (the every-tick exposure re-assert)** | **27.17 ms** | **597.64 ms** |

**The ingest costs 27.62 ms per consumed frame today, and 98 % of it is the exposure re-assert** — a
vendor call that re-asserts a value that has not changed (row #153 owns the fix). Our own work is
0.45 ms. At the source's 24.0 Hz (row #151) today's ingest is **66 % of a core**; at 60 Hz it is
**166 %**. **The pipeline's rate ceiling is a waste, not the machine** — which is the operator's
thesis, now measured.

The contract's ingest budget is therefore **≤ 1.5 ms** per consumed frame (the `post` segment's
measured max, 1.40 ms, with margin) — what the ingest costs once the re-assert is on change only —
and the utilisation table carries today's cost beside it:

| source rate | the period | the budget (≤ 1.5 ms) | today (27.62 ms) |
| --- | --- | --- | --- |
| **24 Hz** | 41.7 ms | **3.6 %** of a core | **66 %** |
| **50 Hz** | 20.0 ms | **7.5 %** | **138 %** — infeasible today |
| **60 Hz** | 16.7 ms | **9.0 %** | **166 %** — infeasible today |

## The presets and their budgets (the contract's §2.2)

The **floor is not a named machine** — the operator's answer (row #154) is to *measure* it: a startup
probe classifies the host into a preset, and the presets' budgets are what each must meet. The M1
stays the reference point they are calibrated against.

| | **High** | **Balanced** | **Lite** | **Capture-only** |
| --- | --- | --- | --- | --- |
| the publish rate | 24 Hz | 24 Hz | 12 Hz | — |
| the point budget | 20,000 | 12,000 | 5,000 | 0 |
| the colour axis | on | on | off | off |
| capture | on | on | on | on |
| the browser's accumulation cap | 76 MB | 40 MB | 15 MB | 0 |
| **CPU — the encode** | ≤ 820 µs | ≤ 490 µs | ≤ 205 µs | 0 |
| **CPU — the broadcast, per client** | ≤ 50 µs | ≤ 23 µs | ≤ 10 µs | 0 |
| **CPU — the tick's work, per frame** | ≤ 1.0 ms | ≤ 0.6 ms | ≤ 0.25 ms | 0 |
| **memory — the server's live footprint** | ≤ 128 KB | ≤ 128 KB | ≤ 128 KB | ≤ 128 KB |
| **memory — the browser's accumulation** | ≤ 76 MB | ≤ 40 MB | ≤ 15 MB | 0 |
| **bandwidth — per client** | ≤ 6.0 MB/s | ≤ 3.5 MB/s | ≤ 0.75 MB/s | 0 |
| **latency — the frame age** | ≤ 5.0 ms | ≤ 4.5 ms | ≤ 4.0 ms | n/a |
| **latency — the user-visible age** | ≤ 50 ms | ≤ 45 ms | ≤ 90 ms | n/a |

Each row's derivation is in `perf-budgets.json` (the encode from the dataset's median at 20,000
points, scaled by the preset's point budget; the bandwidth from the frame's bytes × the rate; the
browser's cap from #136's 75.9 MB; the footprint from the measured 100,352 B).

## The degradation ladder (the contract's §4)

The ladder sheds **quality, not frames**, in #148's order: **the point budget → the colour axis → the
full-detail mesh stream → the publish rate (last, because it trades the latency the pipeline exists
for) → the client count**. It cannot touch the ingest, the pose stream, the capture, or the pacing.

* **Step down.** A *miss* is a stage over its budget (#141's instrument detects it). A **sustained** miss
  — over budget for **1 second** — steps the pipeline down **one** preset, records the step, and
  shows it in the status message. One second, not one tick: a single slow tick is the machine.
* **Step up (the recovery rule, which #148 assigns to this row).** Only when the stage has been
  **within its budget for 10 seconds**, its value is at **≤ 50 % of the budget**, and **30 seconds**
  have passed since the last step. That is the hysteresis: a pipeline merely at its budget does not
  climb, so a borderline host does not oscillate.
* **The ceiling.** The step-up never goes above the preset **the operator chose**; the ladder may step
  *below* a manual choice (an overrunning budget is worse than a lower quality) but never above it.
* **The floor.** Never below **Capture-only**: the ingest continues, the capture is kept, the scan is
  not lost.

## The full-frame mode (row #156's deliverable)

One frame through every stage, timed end to end — the synthetic server encodes and broadcasts it, and
a consumer probe receives it and measures the **age** (the capture stamp → the consumer's receipt). The
per-stage split comes from the server's own `[perf] window` line, so the stages and the total are the
same run. From the raw log:

```
stage                             value     unit  where it comes from
encode + post                     0.070       ms  the server's [perf] window: post p50
broadcast                         0.030       ms  the server's [perf] window: bcast p50
transfer + consume                0.099       ms  the age minus the two stages above
TOTAL (the frame age)             0.199       ms  the consumer's measurement: p50
  the age at p95                  0.377       ms  the consumer's measurement
  the age at max                  0.599       ms  the consumer's measurement
```

**The total is the regression threshold**: the harness fails when it regresses (the preset's
`e2e_frame_age_ms`). The mode is device-free, bounded and runnable on demand and in CI.

**The presets' knobs are not yet settable**: the point budget and the publish rate need the server's
`--synthetic-load` mode (row #151, PR #366), which is not on main. The harness tries it and falls back
to the plain synthetic path, **and its report says which configuration it measured** — it does not
silently pretend the knobs were set.

## The waste inventory — the three biggest

| the waste | the price (measured) |
| --- | --- |
| **the every-tick exposure re-assert** (`main.cpp:1148-1151`) | **27.17 ms mean, 597.64 ms max** per tick (#141's live split) — **98 % of the ingest**, 60× our own work. #153 owns the fix |
| **the per-byte `push_back` encoder** (`frame_protocol.h:39-73`) | **63 % of the tick's CPU** with no client (67 ms of 106 ms) — 123 µs of the 195 µs tick, on every frame |
| **the WebSocket send** (two `send`s per client per frame, `ws_broadcast_server.cpp:255-256`) | **57.7 % of the tick's CPU at 8 clients** (153 ms of 265 ms) — linear in the client count, 435 syscalls/s at 8 clients |

(The full inventory prices seven items — the two-vector copy, the per-tick allocations, the msgpack
byte-at-a-time parse and the broadcast's stall risk as well.)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | The harness runs device-free and bounded | **[PASS]** ~13 s; the server is stopped with `SIGTERM` (then `kill`) and no process is left behind (checked with `pgrep` after each run) |
| 2 | Every measured budget is met | **[PASS]** 11 budgets, 11 `PASS`, exit 0, over the three-run and the 8-run dataset |
| 3 | The budgets are derived from a dataset that is **in the record**, and the three files cannot drift | **[PASS]** the 8 runs are in the raw log with a min/median/max summary computed from them; `--sync-dataset` copies the log's summary into the budget file, recomputes each K and fills each derivation's dataset token; `--check-dataset` compares the file, the contract and the log and **fails if they differ** (`the budget file's dataset matches the log` / `the contract quotes the same dataset`) |
| 4 | Each budget catches the regression size the contract says | **[PASS]** every CPU budget is K × the recorded median, and K is stated per row: **1.52 / 1.53** (the encoder), **2.30** (the broadcast), **1.10 – 1.27** (the platform's wait floors), **1.31** (the footprint). The tick's post segment has **no K** — the dataset holds one sample, so it gates no specific size, and the contract says so |
| 5 | The reference machine keeps up at every source rate | **[PASS]** 3.6 % of a core at 24 Hz, 7.5 % at 50 Hz, 9.0 % at 60 Hz (the budget); and today's measured cost — 66 % / 138 % / 166 % — is printed beside it, so the waste is visible |
| 6 | The lower-end target's projection is reported, not gated | **[PASS]** the `OVER (target)` rows; the harness exits **0** — that is a finding for the operator, not a regression of the code |
| 7 | The full-frame mode measures every stage and the total, and the total has a threshold | **[PASS]** the per-stage table and the total from one run; `the total's budget 5.000 ms PASS`; the mode reports the configuration it measured |
| 8 | The presets' budgets are stated per preset, for CPU, memory, bandwidth and latency | **[PASS]** the contract's §2.2 and `perf-budgets.json`'s `presets` — four presets × six budget kinds, each with its derivation |
| 9 | The degradation ladder's rules are stated | **[PASS]** the contract's §4: the ordered knobs, what each costs in quality, the 1-second sustained-miss step-down, the 10 s / ≤ 50 % / 30 s step-up hysteresis, the operator's preset as the ceiling and Capture-only as the floor |
| 10 | No product behaviour changed | **[PASS]** the diff is: a new target (`tools/scan_server/perf_budget.cpp`, one line in `CMakeLists.txt`), a new script (`scripts/perf-budget.py`), and new documents under `docs/recomp/`. No product source file was touched |
| 11 | The tool's own test suite still passes | **[PASS]** `ctest -LE hardware`: 9/9 |

## Crash sweep

Not run — hardware-free, no vendor SDK linked, no device process involved; per the exemption stated
above and in the #117, #120, #122 and #127 records.

## What this record does **not** answer

* **The exposure re-assert's saving is not yet measured.** #153 owns the fix (a dirty flag) and
  #141's next live run measures the `exposure` segment after it. The contract budgets the ingest as
  if the fix were in (≤ 1.5 ms) and states today's cost beside it (27.62 ms); the saving is the size
  of the gap — the largest single win in the pipeline.
* **The probe is #154's**, and the *thresholds* that classify a host into a preset are its. This
  contract states what each preset must meet; which host lands in which preset is the probe's
  decision, and the user may choose above their tier.
* **The browser's render cost is unmeasured** — the volumes and the page's memory are budgeted; the
  render and the drop rate are #155's and #151's browser side.
* **The frame age's transfer half is the kernel's.** The device-free measurement (0.19 ms p50) is on a
  loopback; the real network and the real browser add to it, and #142's frame age closes that gap.
* **The ladder's thresholds are stated rules, not measurements** (1 s of misses, 10 s within budget
  at ≤ 50 %, 30 s between steps). They are the kind of number that wants a field run; the contract
  names them so they can be argued with.

## Board

- Row move: row #152 reads `TODO` → **`DONE`** — the contract (the reference profile, the per-preset
  budgets, the ladder, the waste inventory), the machine-readable budgets and the harness exist, and
  **the harness passes on current `main`** (11/11 budgets, exit 0, over the 8-run dataset). The
  *live* measurements stay owed to #141, #142 and #151, and are named as open questions here.
- Row #156's deliverable — the full-frame harness mode, the per-stage table, the total and the
  regression threshold on it — **lands in this PR** (the contract's §5.3, the record's full-frame
  section, and `--full-frame` in the harness). The dispatcher moves that row.
