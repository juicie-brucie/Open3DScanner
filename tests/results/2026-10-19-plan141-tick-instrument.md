# PLAN #141 — the tick instrument completed (device-free)

**Date**: 2026-10-19
**Target**: `tools/scan_server/main.cpp` + `perf_window.h` (device-free run)
**Command**: `scan_server --synthetic 19141` (12 s)
**Exit**: 0

No device was used. This is the mock/synthetic evidence; the device-side
distribution is the operator's run, so row #141 is a **hardware** row.

## The new line (verbatim)

```
[perf] window 5.0s clock=steady_clock n=136 rate=27.0/s points=2304.0 bytes=27697.6 | pose p50=0.00 p95=0.00 max=0.00 ms | getter p50=0.00 p95=0.00 max=0.00 ms | post p50=0.11 p95=0.22 max=0.38 ms | bcast p50=0.00 p95=0.00 max=0.00 ms | wait p50=38.01 p95=38.04 max=38.08 ms | exposure p50=0.00 p95=0.00 max=0.00 ms | release p50=0.00 p95=0.00 max=0.00 ms | overshoot p50=4.68 p95=4.71 max=4.74 ms | residual p50=0.00 p95=0.01 max=0.50 ms | tick p50=38.08 p95=38.25 max=38.31 ms | nominal=33.33 ms | clients=0.0 | bcast_per_client=0.00 us (derived mean; per-client send stats need engine/**)
```

### Schema (every field names its clock and unit)

- `clock=steady_clock`, times reported in **ms** (steady_clock µs internally),
  window = 5 s, instrument = `TickPerf`.
- The four historical segments, unchanged in name/index/meaning: **`pose`**,
  **`getter`**, **`post`**, **`bcast`**.
- **`wait`** — the pacing call (#139); **`overshoot`** = `wait − nominal`.
- **`exposure`** — `obscan_scan_config_combined_exposure_and_laser_current`
  (re-asserted every tick when auto-exposure is off); previously untimed.
- **`release`** — `obscan_scan_model_clouds_release`; previously untimed.
- **`residual`** = `tick_total − Σ segments` — the untimed remainder.
- **`tick`** — the realized period; **`nominal`** — the pacing budget.
- **`clients`** — connected clients at broadcast time; **`bcast_per_client`** —
  the derived mean (`bcast p50 / clients`).

The identity holds here: `wait 38.01 + post 0.11 + residual 0.00 ≈ tick 38.08`.

## Broadcast counters — what is and is not visible

The aggregate `bcast` segment and the client count are measured. **Per-client
send time, queue depth, drops and send timeouts are not reachable from
`tools/scan_server/**`**: `WsBroadcastServer` is a pimpl with only
`BroadcastBinary`/`BroadcastText`/`ConnectedClientCount`, and the 100 ms
`SO_SNDTIMEO` plus the drop-on-failed-send live in
`engine/src/ws_broadcast_server.cpp`. Exposing them needs an accessor in
`engine/**`, outside this row's wall — filed as the follow-up rather than
smuggled in. `bcast_per_client` above is the in-wall derived proxy.

## Device-free coverage

`perf_window_test` (ctest, not hardware-labelled) asserts: the four work
segments keep indices/names; wait/exposure/release are separate; `overshoot =
wait − nominal` (including a negative case); **`residual = period − Σ
segments`** (exact, via the `BeginTickAt` seam); and the snapshot does not reset.

```
[perf_window_test] PASS: 4 work segments unchanged; wait/exposure/release separate; overshoot = wait - nominal; residual = period - sum
```

Raw output: `2026-10-19-plan141-tick-instrument.log`.
