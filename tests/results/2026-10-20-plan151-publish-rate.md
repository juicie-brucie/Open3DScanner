# PLAN #151 — what publish rate can the pipeline sustain? (device-free)

**Date**: 2026-10-20
**Harness**: `scan_server --synthetic-load <port> <hz> <20000>` (a test-only mode;
the default path is unchanged), consumer `tools/scan_server/ws_probe.py` (a
minimal WebSocket client — the browser HUD's own counter would need a browser,
which this harness does not start; #359's frame-age field is **not on main**, so
this uses the frame counter and data fps).
**Rates**: 10 / 24 / 50 Hz, 20,000-point frames, 1 and 8 clients, 8 s windows.
**No device.**

## Headline numbers

| request | clients | realized | post p50 | bcast p50 / p95 | wait p50 | overshoot p50 | tick p50 | consumer per-client fps |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 10 Hz | 1 | 9.6 /s | 0.86 ms | 0.06 / 0.15 ms | 104.02 ms | 4.02 ms | 105.16 ms | 9.67 |
| 10 Hz | 8 | 9.6 /s | 0.70 ms | 0.37 / 0.85 ms | 103.00 ms | 3.00 ms | 104.42 ms | 9.71 |
| 24 Hz | 1 | 22.0 /s | 0.79 ms | 0.05 / 0.12 ms | 45.02 ms | 3.35 ms | 46.22 ms | 22.23 |
| 24 Hz | 8 | 22.2 /s | 0.71 ms | 0.35 / 0.73 ms | 44.24 ms | 2.58 ms | 45.22 ms | 22.24 |
| 50 Hz | 1 | 41.6 /s | 0.84 ms | 0.08 / 0.19 ms | 23.51 ms | 3.51 ms | 24.65 ms | 41.52 |
| 50 Hz | 8 | 42.2 /s | 0.97 ms | 0.73 / 1.34 ms | 22.34 ms | 2.34 ms | 24.34 ms | 42.35 |

The realized rate is the *server's* tick (`rate=` on the `[perf]` line); the
consumer's per-client fps matches it in every cell (9.7 / 22.2 / 42.3), and the
8-client aggregate is 8× (77.7 / 177.9 / 338.8 fps) — **linear, no drops and no
send timeouts** (the probe reports no errors; every expected frame arrived). The
broadcast p50 grows from ~0.06 to ~0.73 ms at 8 clients, still ~1–3 % of the
tick.

## The binding limit at each rate

| rate | binding limit | the number that proves it |
| --- | --- | --- |
| 10 Hz | **our pacing floor** | nominal 100 ms, `wait` p50 104.0 ms (overshoot +4.0), work ≈ 0.9 ms → 9.6 Hz |
| 24 Hz | **our pacing floor** — it cannot quite match the 24 Hz source | nominal 41.67 ms, `wait` p50 45.0 ms (overshoot +3.35), work ≈ 0.8 ms → 22.0 Hz (91.5 % of the source) |
| 50 Hz | **our pacing floor** | nominal 20 ms, `wait` p50 23.5 ms (overshoot +3.51), work ≈ 0.9 ms → 41.6 Hz (83 %) |

In every cell the **encoder + broadcast work is ~1.0–1.7 ms combined**, an order
of magnitude below the **2.3–4.0 ms pacing overshoot**, and the untimed
`residual` is ≈0. So the limit is neither the encoder (63 % of *the tick's CPU*
in #147 is 63 % of a small number) nor the broadcast (linear, sub-millisecond);
it is the **pacing floor of the run-loop/sleep** on this machine. At 20,000
points the synthetic encoder is ~0.9 ms p50, so a 20 ms budget would fit — the
~3.5 ms sleep overshoot plus the ~1 ms work pushes the realized period to 24 ms.

### What the source rate means

The #112 log's 24.0 Hz is the **fused-cloud frame rate the listener receives**
(two log lines per frame), not the profile's advertised 50 fps at 1920×1200
format 9 — the raw sensor rate is not visible through `FRAME_READY`. So the
24 Hz request in this table is the right comparison for the pipeline; the raw
50 fps is not a publish target the fused path can offer.

## What this does not settle

- The live loop paces with `CFRunLoopRunInMode(…, 0.1, false)` rather than
  `sleep_for`; the #112 run measured a 103 ms floor there, consistent with this
  harness's floor, but the device-side distribution at 24/50 Hz is a hardware
  step.
- The browser's own render loop and the frame-age metric (#359) were not
  measured; the WS probe stands in for the consumer.

Raw output: `2026-10-20-plan151-publish-rate.log`.
