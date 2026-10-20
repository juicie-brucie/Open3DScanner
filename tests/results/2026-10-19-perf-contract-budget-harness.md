# PLAN #152 — the performance contract + the repeatable budget harness — PASS (device-free)

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-build4
- Branch: build/plan152-perf-contract
- Target: build/tools/scan_server/perf_budget (new, build-tree copy) + build/tools/scan_server/scan_server
- Other copy: none
- Preflight/postflight: **not run** — no vendor SDK is linked by either binary (`perf_budget` links
  `openscanner_engine` only; the synthetic `scan_server` path has no device), and the harness never
  contacts a device. No interposer and no crash-report sweep apply.
- Interposer: n/a — exempt, as for the #117, #120, #122 and #127 records.
- Run start: 2026-10-19 (the raw log carries the exact command sequence)
- Device: **not used**.

## What this PR delivers

Row #152: the operator's standard — the ingest must be **as fast as possible**, because extending
Creality Scan's features and running on **lower-end hardware** depends on it.

1. **The contract** — `docs/recomp/PERFORMANCE-CONTRACT.md`: the reference profile (the measured M1
   baseline from #147 + the **stated** lower-end assumption and the factor every budget carries), the
   explicit budgets per stage, and the priced waste inventory.
2. **The machine-readable budgets** — `docs/recomp/perf-budgets.json`: the same budgets with the
   measurement each derives from, so the contract and the check cannot drift apart.
3. **The harness** — `scripts/perf-budget.py` + `tools/scan_server/perf_budget.cpp`: one command that
   measures the device-free surfaces, prints the per-stage numbers, and **fails if a budget
   regresses**.

**No product behaviour changed.** The diff adds a benchmark target, a script and two documents; no
line of the product's own code was touched.

## Commands

```
# the harness: the device-free surfaces, the per-stage numbers, the verdicts
python3 scripts/perf-budget.py

# a smoke run (numbers only, no verdicts, no server)
python3 scripts/perf-budget.py --quick

# check the budgets as if this machine were 3x slower (the lower-end target)
python3 scripts/perf-budget.py --factor 3
```

It runs the synthetic `scan_server` (no vendor SDK, no device) for a fixed number of seconds and the
`perf_budget` binary — which measures **the product's own encoder and WebSocket broadcast** in
process — then compares every measurement with `docs/recomp/perf-budgets.json`.

## Result

- Exit code: **0** for the full run (three consecutive runs: 0, 0, 0)
- Verdict: **PASS** (device-free) — every measured budget met on current `main`
- Raw log: `2026-10-19-perf-contract-budget-harness.raw.log` (same directory)

## The budgets, as the harness printed them

```
metric                                 measured       budget         verdict
platform.wait_ms.41                      41.080       47.000   PASS  #147 section 5 measured 44.62 ms for a 41 ms request (zero work in the l
platform.wait_ms.20                      24.340       26.500   PASS  #147 section 5 measured 24.29 ms for a 20 ms request; the ceiling for a
platform.wait_ms.16                      18.788       21.500   PASS  the same control at the 60 Hz pace (16 ms request); measured by this har
encode.bytes.live                    240036.000   240036.000   PASS  #136's live V1 frame: 20,000 points (kLiveBroadcastPointBudget) = 240,03
encode.us.live                          564.958     1200.000   PASS  the live post segment measured 1.18-1.68 ms at p50 in #112 covers decode
encode.us.2304                           64.167      300.000   PASS  #147 section 7: the encoder is 63% of the tick's CPU with no client, i.e
bcast.us_per_client.8                    20.943       60.000   PASS  #147 section 6 measured 35.9-38.4 us per client per tick at 8 clients (t
bcast.us.8                              167.542      400.000   PASS  #147 section 6: the bcast segment was 0.29-0.31 ms per tick at 8 clients
tick.post_ms                              0.100        0.400   PASS  #147 section 3 measured the synthetic tick's post segment at 0.12-0.20 m
tick.bcast_ms                             0.000        0.250   PASS  #147 section 3 measured the bcast segment at 0.10 ms p50 with one client
server.live_bytes                    100352.000   131072.000   PASS  #147 section 3.2 measured the live allocation footprint flat at 86-87 KB
```

And the ingest budget, per consumed frame, at each source rate:

```
rate      ceiling     target    ms/frame core % nominal core % at ceiling verdict
24          24.00         M1        8.00           19.2             19.2   PASS
24          24.00  lower-end       24.00           57.6             57.6   PASS
50          41.08         M1        8.00           40.0             32.9   PASS
50          41.08  lower-end       24.00          120.0             98.6 OVER (target)
60          53.23         M1        8.00           48.0             42.6   PASS
60          53.23  lower-end       24.00          144.0            127.7 OVER (target)
```

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | The harness runs device-free and bounded | **[PASS]** the full run takes ~13 s; the synthetic server is stopped with `SIGTERM` (then `kill`) and no process is left behind (checked with `pgrep` after each run) |
| 2 | Every measured budget is met on current main | **[PASS]** the table above — 11 budgets, 11 `PASS`, harness exit `0`, over three consecutive runs |
| 3 | The harness fails when a budget regresses | **[PASS]** the mechanism is the comparison in `scripts/perf-budget.py`; it was observed failing (`2 budget(s) REGRESSED`, exit 1) while the broadcast measurement was still contended, and the fix was to the *measurement* (a 4 MB receive buffer on the harness's client), not to the budget |
| 4 | The reference machine keeps up at every source rate | **[PASS]** 19.2 % of a core at 24 Hz, 40.0 % at 50 Hz, 48.0 % at 60 Hz (the utilisation at the nominal rate; the harness also prints it at the platform's measured ceiling) |
| 5 | The lower-end target's projection is reported, not gated | **[PASS]** the two `OVER (target)` rows: at 50 Hz the assumed target needs 120 % of a core, at 60 Hz 144 %. The harness exits **0** — that is a finding for the operator, not a regression of the code |
| 6 | The budgets are derived from measurements, not impressions | **[PASS]** every row of `perf-budgets.json` carries a `derived_from` naming its source (#147 §3/§5/§6/§7, #112, #136, #153), and the contract's tables carry the same citations |
| 7 | The lower-end factor is labelled everywhere | **[PASS]** the contract's §1.2 states the assumption ("a 4-core Intel machine, ~3× slower"; the operator has not named the target), `perf-budgets.json` carries `lower_end_factor: 3.0` and `lower_end_status`, and the CPU budgets are marked `factor: true` while the data-volume budgets are marked `factor: false` |
| 8 | No product behaviour changed | **[PASS]** the diff is: a new target (`tools/scan_server/perf_budget.cpp`, one line in `CMakeLists.txt`), a new script (`scripts/perf-budget.py`), and two new documents under `docs/recomp/`. No product source file was touched |
| 9 | The tool's own test suite still passes | **[PASS]** `ctest -LE hardware`: 9/9 |

## The three biggest wastes, with their prices

From the contract's §3 (each priced from #147's profile and the source):

1. **The per-byte `push_back` encoder** (`frame_protocol.h:39-73`) — **63 % of the tick's CPU with no
   client** (67 ms of 106 ms in a 20 s Time Profiler run). 123 µs of the 195 µs tick → 3.34 ms of CPU
   per second at 27.2 ticks/s; on the live path ≈ 0.6 – 1.1 ms/frame → 14 – 26 ms/s at 24 Hz (1.4 –
   2.6 % of a core), ×3 on the assumed target: 4.2 – 7.8 %.
2. **The WebSocket send** (`ws_broadcast_server.cpp:255-256` — the header and the payload are two
   `send`s per client per frame) — **57.7 % of the tick's CPU at 8 clients** (153 ms of 265 ms). 297 µs
   of the 514 µs tick → 8.1 ms of CPU per second; 435 syscalls/s at 8 clients.
3. **The every-tick exposure re-assert** (`main.cpp:1148-1151`) — one of the two untimed vendor calls
   that make up most of the live tick's ~5 – 6 ms unsegmented work (#153). ≤ 6 ms per tick → 52 ms of
   CPU per second at 8.7 Hz (5.2 % of a core); at 24 Hz, 144 ms/s (14.4 %), ×3 on the assumed target:
   43 %.

(The full inventory prices seven items — the two-vector copy, the per-tick allocations, the msgpack
byte-at-a-time parse and the broadcast's stall risk as well — each with its measurement.)

## Crash sweep

Not run — hardware-free, no vendor SDK linked, no device process involved; per the exemption stated
above and in the #117, #120, #122 and #127 records.

## What this record does **not** answer

* **The live path's vendor calls are not measured here** — they need a device. #141's instrument
  times them (`exposure`, `release` segments) but **its live run has not landed** (row #141 is `TODO` on
  main), so the ingest budget carries an **allowance** for them (~6.0 ms, derived from #134's 9.6 ms
  residual less the platform's wait overshoot), not a measurement.
* **The browser is not in the harness.** The volumes the page must absorb are budgeted (2.09 MB/s at
  today's 8.7 Hz; 5.76 MB/s at 24 Hz) and the page's memory is budgeted (≤ 76 MB, #136), but the
  **render cost is unmeasured** — #142 (the HUD's frame age) and #151 (the browser side of the
  publish-rate load test) own it. The contract marks that budget as a placeholder.
* **The lower-end target is an assumption.** The operator has not named the machine; the 3.0× factor
  is a placeholder, and naming it re-bases the contract in one edit (`perf-budgets.json`'s
  `lower_end_factor`, then `--factor N`).

## Open questions

- **What is the target machine?** Everything in the lower-end column moves with that answer.
- **Should the publish rate be raised at all?** The contract shows the code is not the limit at
  24 Hz (19 % of a core) — the platform's timer is, and at 50/60 Hz the assumed target's ingest cost
  is. Raising it is #113's question; this contract states the ceiling the code must fit inside.
- **The broadcast's stall risk has no budget that can be "met"** — it is a shape, not a number: a
  synchronous fan-out under `clients_mutex` with a 100 ms per-client send timeout. One stalled client
  can hold the tick for 2.4 publish periods at 24 Hz. #113/#148 own the structural answer.
- **The harness measures the broadcast with a 4 MB receive buffer on its client**, so its per-client
  number (18 – 23 µs) is lower than #147's tick-condition number (35.9 – 38.4 µs). Both are under
  the 60 µs budget; the difference is the drain wait, and it is named in the contract's §4.

## Board

- Row move: row #152 reads `TODO` → **`DONE`** in this PR — the contract, the machine-readable
  budgets and the harness exist, and **the harness passes on current `main`** (11/11 budgets, exit 0,
  three consecutive runs), which is the row's own condition for `DONE`. The *live* measurements
  (#141's vendor-call split, #142/#151's browser side) are other rows' work and are named as open
  questions here; nothing in this row waits on the device.
