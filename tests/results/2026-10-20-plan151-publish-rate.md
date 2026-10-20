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

---

# Addendum — source cadence by content, per-frame budget, pacing primitive

## 1. Does the cloud content change at the delivery rate?

Instrumentation added: every live tick hashes the cloud (first 256 points + the
point count, FNV-1a) and reports a `[perf] cloud` line every 5 s:

```
[perf] cloud window 5.0s clock=steady_clock ticks=209 changes=208 change_rate=41.6/s run_ticks p50=1 p95=1 max=1
```

Device-free validation (synthetic, where the cloud rotates every tick) gives
`change_rate = 41.6/s` at a 50 Hz request and `run_ticks p50=1`: the tracker sees
every content change and the run-length logic works. **The vendor's own content
cadence is not measurable device-free** — the live loop now emits the same line,
so the operator's run will answer whether the ~24 Hz is a content update or a
delivery rate, and whether the advertised 50 fps ever reaches us. Until then the
24 Hz figure is the *listener's frame count* (#112), not a measured content rate.

## 2. Ingest per source frame vs the budget

Publish-side ingest at 20,000 points (post + encode + broadcast, p50):

| source rate | budget/frame | publish-side ingest | remaining |
| --- | --- | --- | --- |
| 24 Hz | 41.7 ms | 0.84–1.06 ms (1 client); 1.06–1.70 ms (8) | **~40.0–40.9 ms** |
| 50 Hz | 20.0 ms | 0.84–1.07 ms (1); 1.07–1.70 ms (8) | **~18.3–19.2 ms** |
| 60 Hz | 16.7 ms | 0.84–0.97 ms (1) | **~15.7–15.8 ms** |

That leaves substantial room for downstream near-real-time logic **on the
publish side**. It is not the whole ingest: the vendor calls (getter, exposure
re-assert, cloud release) are outside this device-free harness; #134/#136
measured ~9.6 ms/tick outside the four segments, and #141 now splits
`exposure`/`release` (merged) so the operator's live run can complete this
column. If the vendor side is ~9.6 ms, the 60 Hz remainder shrinks to ~6 ms and
the 24 Hz remainder to ~30 ms — still positive, but the vendor-side number is
the one that decides it.

## 3. Pacing primitive: `sleep_until` vs `sleep_for`

`--synthetic-load` gained a `deadline` argument (absolute `sleep_until(next)`).
Measured at 20,000 points, 1 client:

| request | mode | realized | wait p50 | tick p50 |
| --- | --- | --- | --- | --- |
| 50 Hz | sleep_for | 41.7 /s | 22.96 ms | 24.1 ms |
| 50 Hz | deadline | 41.2 /s | 23.77 ms | 24.3 ms |
| 60 Hz | sleep_for | 51.4 /s | 18.77 ms | 19.4 ms |
| 60 Hz | deadline | 51.5 /s | 18.42 ms | 19.3 ms |

The deadline form removes the *accumulated* drift but **does not remove the
wake latency**: at 60 Hz the wait still overshoots by ~1.75 ms (vs ~2.1 ms for
`sleep_for`), and at 50 Hz the deadline case was no better at all (3.8 ms vs
3.0 ms — run-to-run noise dominates there), so 60 Hz realizes **~51.5 Hz
(86 %)** in both modes. The primitive
change is therefore **necessary but not sufficient**: reaching 60 Hz needs a
lower-latency wait (`mach_wait_until` or a tighter timer/thread priority), not
just a smaller timeout or an absolute deadline.

Raw output for this addendum: `2026-10-20-plan151-publish-rate-addendum.log`.
