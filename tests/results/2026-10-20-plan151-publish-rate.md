# PLAN #151 — what publish rate can the pipeline sustain? (device-free)

**Date**: 2026-10-20
**Harness**: `scan_server --synthetic-load <port> <hz> <20000>` (a test-only
mode), consumer `tools/scan_server/ws_probe.py` (a minimal WebSocket client —
the browser HUD's own counter would need a browser, which this harness does not
start). The **frame age and the drop rate** (the row's P3 item) are measured
too, headlessly, in "The frame age and the drop rate (P3)" below: #359 put the
HUD's frame age on the wire and it **is** on `main` (`2f1a4c0`), and
`ui/frame_age_test.js` (ctest `ui_frame_age_load_test`) runs the HUD's own
arithmetic against this harness — no browser needed.
**Rates**: 10 / 24 / 50 Hz, 20,000-point frames, 1 and 8 clients, 8 s windows.
**No device.**

## What changed in the default path

Two diagnostic stdout lines, nothing else. The synthetic server prints
`[synthetic] rate=… points=…` once, and the synthetic **and** live loops print
the periodic `[perf] cloud …` line (addendum §1). Cadence, frames and timings
are unchanged: the base binary (`origin/main`) and this one produce the
**same** `--synthetic` run — same `n=140` ticks, same `rate=27.9/s`, same
`points=2304.0`, same `bytes=27700.4`, same `post p50=0.10`, same
`nominal=33.33 ms` (the platform's own jitter moves the wait's p50 by
~0.35 ms and the post's max by ~0.04 ms) — and differ only in those two lines.
`--synthetic-load` is opt-in behind its own flag; `scan_server <port>` (the
product default) never reaches it.

## Headline numbers

| request | clients | realized | post p50 | bcast p50 / p95 | wait p50 | overshoot p50 | tick p50 | consumer per-client fps |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 10 Hz | 1 | 9.6 /s | 0.86 ms | 0.06 / 0.15 ms | 104.02 ms | 4.02 ms | 105.16 ms | 9.67 |
| 10 Hz | 8 | 9.6 /s | 0.70 ms | 0.37 / 0.85 ms | 103.00 ms | 3.00 ms | 104.42 ms | 9.71 |
| 24 Hz | 1 | 22.0 /s | 0.79 ms | 0.05 / 0.12 ms | 45.02 ms | 3.35 ms | 46.22 ms | 22.23 |
| 24 Hz | 8 | 22.2 /s | 0.71 ms | 0.35 / 0.73 ms | 44.24 ms | 2.58 ms | 45.22 ms | 22.24 |
| 50 Hz | 1 | 41.6 /s | 0.84 ms | 0.08 / 0.19 ms | 23.51 ms | 3.51 ms | 24.65 ms | 41.52 |
| 50 Hz | 8 | 42.2 /s | 0.97 ms | 0.73 / 1.34 ms | 22.34 ms | 2.34 ms | 24.34 ms | 42.35 |

The realized rate is the *server's* tick (`rate=` on the `[perf]` line). The
consumer's per-client fps tracks it in every cell — 9.67 / 9.71 at 10 Hz,
22.23 / 22.24 at 24 Hz, 41.52 / 42.35 at 50 Hz — and the 8-client aggregate
is 8× (77.7 / 177.9 / 338.8 fps): **linear, with no probe error and no client
dropped**. What that evidences is the *rate*. The probe counts frames; it does
not check frame identity, so "every expected frame arrived" is **not** what it
shows — the frame-id check is the P3 section's, and it is a separate
measurement. A failed or timed-out send makes the server drop that client
(`engine/src/ws_broadcast_server.cpp:231`), and no client was dropped.

The broadcast p50 grows from ~0.06 ms at one client to 0.37–0.73 ms at eight
— **0.35 %** of the tick in the 10 Hz 8-client cell (0.37 of 104.42), **0.77 %**
at 24 Hz (0.35 of 45.22) and **3.0 %** at 50 Hz (0.73 of 24.34).

## The binding limit at each rate

| rate | binding limit | the number that proves it |
| --- | --- | --- |
| 10 Hz | **our pacing floor** | nominal 100 ms, `wait` p50 104.0 ms (overshoot +4.0), work ≈ 0.9 ms → 9.6 Hz |
| 24 Hz | **our pacing floor** — it cannot quite match the 24 Hz source | nominal 41.67 ms, `wait` p50 45.0 ms (overshoot +3.35), work ≈ 0.8 ms → 22.0 Hz (91.5 % of the source) |
| 50 Hz | **our pacing floor** | nominal 20 ms, `wait` p50 23.5 ms (overshoot +3.51), work ≈ 0.9 ms → 41.6 Hz (83 %) |

In every cell the **encoder + broadcast work is 0.84–1.70 ms combined** — per
cell, **1.4–4.4× below that cell's pacing overshoot** (2.34–4.02 ms) — and the
untimed `residual` is ≈0. So the limit is neither the encoder (63 % of *the
tick's CPU* in #147 is 63 % of a small number) nor the broadcast (linear,
sub-millisecond); it is the **pacing floor of the run-loop/sleep** on this
machine. At 20,000 points the synthetic encoder is ~0.9 ms p50, so a 20 ms
budget would fit — the ~3.5 ms sleep overshoot plus the ~1 ms work pushes the
realized period to 24 ms.

### What the source rate means

The #112 log's 24.0 Hz is the **fused-cloud frame rate the listener receives**
(two log lines per frame), not the profile's advertised 50 fps at 1920×1200
format 9 — the raw sensor rate is not visible through `FRAME_READY`. So the
24 Hz request in this table is the right comparison for the pipeline; the raw
50 fps is not a publish target the fused path can offer.

## The frame age and the drop rate (P3)

#359 put the HUD's frame age on the wire and it **is** on `main` (`2f1a4c0`):
the status message carries `server_now_ms`, the V1 envelope carries
`t_capture`, and `ui/frame_age.js` turns the pair into
`age at receipt = (local wall + offset) - t_capture`. The check is headless —
`ui/frame_age_test.js` (ctest `ui_frame_age_load_test`) runs the HUD's own
arithmetic against this harness's `--synthetic-load` mode — so no browser is
needed. An earlier draft of this record said the metric was not on `main` and
skipped the row's P3 item on that premise; the premise was false.

| request | realized | frames | published | missing | drop rate | frame age p50 / p95 | min / max |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 24 Hz | 22.9 /s | 127 | 127 | 0 | **0.00 %** | **0.8 / 2.1 ms** | -0.2 / 3.2 ms |
| 50 Hz | 43.2 /s | 239 | 239 | 0 | **0.00 %** | **1.4 / 2.5 ms** | -0.5 / 8.3 ms |

Raw output: `2026-10-20-plan151-publish-rate-frame-age.log` — the test's own
output, with the server's `[perf]` lines.

**What the drop rate is.** The probe reads the V1 envelope's `seq` and checks
it for continuity: `published` is the number of frames the server published
between the first and the last one the client saw (`last_seq - first_seq + 1`)
and `missing` is the gap in that run. Both cells ran a **contiguous** sequence
— **0 frames dropped** at 24 Hz and at 50 Hz — and the client's own rate
(22.90/s, 43.19/s) matched the server's (22.9/s, 43.2/s). The frames that
arrive before the first status message carry no clock and are not counted:
they are outside the drop count, not inside it.

**What the age is.** The median age is the tick's publish cost plus the
client's handling delay: the same 0.66–0.67 ms of `post`+`bcast` reads as a
0.8 ms median at 24 Hz and 1.4 ms at 50 Hz — the difference is the consumer's
event loop, which has twice as many frames to take in the same second. The
**maximum** (8.3 ms at 50 Hz) is the consumer's scheduling too, not the
server's: the same run's server-side `tick p50` is 23.55 ms and its `post p50`
0.60 ms.

**The negative minimum is the clock's granularity, not a defect.** The server
emits `server_now_ms` as an integer millisecond
(`static_cast<long long>(NowMs())`) while `t_capture` is a float64 millisecond,
so the arithmetic can read about a millisecond below the truth. The browser's
copy of the same arithmetic has the same artifact.

## What this does not settle

- The live loop paces with `CFRunLoopRunInMode(…, 0.1, false)` rather than
  `sleep_for`; the #112 run measured a 103 ms floor there, consistent with this
  harness's floor, but the device-side distribution at 24/50 Hz is a hardware
  step. The deadline result (addendum §3) is a property of the
  `std::this_thread` wait, not of `CFRunLoopRunInMode`: it says the primitive
  is **necessary**, not that it is sufficient there.
- The **browser's render loop** is not measured. The P3 section's frame age is
  the **tick → receipt** leg: `t_capture` is the tick's read time (#136 stage
  1), not a device capture stamp, and the local render delay is stubbed at
  1 ms. The browser's own queue between receipt and paint, and the rate the
  page actually paints at, are still unmeasured — #142's note records that a
  proxied-browser read measured the *transport*, not the pipeline.
- The **vendor's content cadence** (addendum §1) and the device-side rate
  distribution are owed a device run.

Raw output: `2026-10-20-plan151-publish-rate.log`.

---

# Addendum — source cadence by content, per-frame budget, pacing primitive

## 1. Does the cloud content change at the delivery rate?

Instrumentation added: every live tick hashes the cloud and reports a
`[perf] cloud` line every 5 s:

```
[perf] cloud window 5.0s clock=steady_clock ticks=299 changes=298 change_rate=59.6/s run_ticks p50=1 p95=1 max=1
```

That is the 60 Hz deadline run's line, from
`2026-10-20-plan151-publish-rate-addendum.log`; the other three runs in that
log read `change_rate=43.6/s` (50 Hz, `sleep_for`), `49.6/s` (50 Hz,
`deadline`) and `54.8/s` (60 Hz, `sleep_for`), each with `run_ticks p50=1`.

**Limitation (disclosed):** the hash covers the **first 256 points plus the
count** (FNV-1a), a cheap proxy — a change confined to points beyond the 256th
with an identical prefix would be missed. It is not a full-cloud digest; the
live run can widen the window if the first result looks suspicious.

Device-free validation (synthetic, where the cloud rotates every tick) gives
`change_rate` = the publish rate and `run_ticks p50=1`: the tracker sees every
content change and the run-length logic works. **The vendor's own content
cadence is not measurable device-free** — the live loop now emits the same line,
so the operator's run answers whether the ~24 Hz is a content update or a
delivery rate, and whether the advertised 50 fps ever reaches us. Until then the
24 Hz figure is the *listener's frame count* (#112), not a measured content rate.

## 2. Ingest per source frame vs the budget

Publish-side ingest (post + encode + broadcast, p50) at 20,000 points, from the
raw lines in the two logs:

| source rate | budget/frame | 1 client | 8 clients | remaining (1 client) |
| --- | --- | --- | --- | --- |
| 24 Hz | 41.7 ms | 0.84 ms (post 0.79 + bcast 0.05) | 1.06 ms (0.71 + 0.35) | **~40.9 ms** |
| 50 Hz | 20.0 ms | 0.64 ms (0.59 + 0.05) | 1.70 ms (0.97 + 0.73) | **~19.4 ms** |
| 60 Hz | 16.7 ms | 0.66 ms (0.61 + 0.05) | not measured | **~16.0 ms** |

The 60 Hz row is now backed by the real 60 Hz run (`post p50=0.61`,
`bcast p50=0.05`); the 8-client cell is left blank because no 60 Hz 8-client run
was made.

**This column is the conservative case.** The synthetic `post` carries two
costs the live path does not have: the harness's per-tick **240 KB copy** of
the point vector (`tools/scan_server/main.cpp:573`) and §1's content hash (256
points, FNV-1a). Per #148 the live `post` costs **37.5 ns/point** against the
synthetic encoder's **53.4 ns/point**, so the live path's ingest at 20,000
points is cheaper than the figures above.

That leaves substantial room for downstream near-real-time logic **on the
publish side**. It is not the whole ingest: the vendor calls (getter, exposure
re-assert, cloud release) are outside this device-free harness; #134/#136
measured ~9.6 ms/tick outside the four segments, and #147 attributes
**~3.2–3.9 ms of that 9.6 ms to the platform's own wait overshoot**, leaving
**~5.7–6.4 ms of vendor work**. Taking the pessimistic bound (the whole
9.6 ms), the 60 Hz remainder shrinks to ~6.4 ms and the 24 Hz remainder to
~31.3 ms; with #147's vendor-only split they are ~9.6–10.3 ms and
~34.5–35.2 ms. Either way they are positive — but the vendor-side number is
the one that decides it. #141 now splits `exposure`/`release` (merged), so the
operator's live run completes this column.

## 3. Pacing primitive: `sleep_until` vs `sleep_for`

`--synthetic-load` gained a `deadline` argument (absolute
`sleep_until(next_deadline)`). Measured at 20,000 points, 1 client, 6 s, in this
re-run (raw lines in `2026-10-20-plan151-publish-rate-addendum.log`):

| request | mode | realized | wait p50 | overshoot p50 | tick p50 |
| --- | --- | --- | --- | --- | --- |
| 50 Hz | sleep_for | 43.7 /s (87.4 %) | 21.83 ms | **+1.83 ms** | 22.49 ms |
| 50 Hz | deadline | **49.8 /s (99.6 %)** | 19.20 ms | **−0.80 ms** | 19.84 ms |
| 60 Hz | sleep_for | 54.9 /s (91.5 %) | 17.04 ms | **+0.37 ms** | 17.78 ms |
| 60 Hz | deadline | **59.8 /s (99.7 %)** | 15.90 ms | **−0.77 ms** | 16.57 ms |

**Correct conclusion: on the `std::this_thread` wait, the absolute-deadline
primitive is the fix** — scoped to *that* wait. The live loop paces with
`CFRunLoopRunInMode`, not `sleep_until`, so this says the primitive is
**necessary**, not that changing it in the live loop is sufficient (see "What
this does not settle"; #148's C2 seam proposes the same primitive for the live
path). In the deadline path the period is the deadline advance itself, so the
realized rate tracks the request: **99.7 % of 60 Hz and 99.6 % of 50 Hz**, with
a *negative* p50 overshoot (the deadline is advanced before the work is
charged, so the p50 wait reads 0.80 ms short of the nominal). Wake latency is
therefore **not** the binding limit at these rates.

**The mechanism, stated correctly.** The `sleep_for` path waits a *relative*
interval, and two things follow from that: the target is the nominal
**truncated to whole milliseconds** (`milliseconds(int(interval_ms))` — 16 ms
at 60 Hz, 0.67 ms short of 16.67), and the tick's elapsed time is subtracted
from it only in whole milliseconds. Its period is therefore
`floor(interval) + frac(work) + wake latency`: the recorded 50 Hz cell reads
20 + 0.65 + 1.83 ≈ 22.49 ms tick p50, and the 60 Hz cell 16 + 0.66 + 1.04 ≈
17.7 ms (tick p50 17.78). It is the **wake latency** and the **truncation**,
not the work, that cost it the rate — the work contributes only its
sub-millisecond remainder. The deadline cell's latencies telescope away: each
wake's overshoot is paid back by the next period, so the period is the deadline
advance (20.000 ms at 50 Hz; at 60 Hz the advance is 16,666 µs against a true
16,666.7 µs — 0.67 µs a tick, 0.04 Hz). That is why it lands at 87–92 % →
99.6–99.7 %.

*Correction note:* an earlier draft's "deadline" rows (periods 19.4 / 24.3 ms,
positive overshoot) were produced by a **harness-argument error** — the mode word
was passed in the `<points>` position, so those runs were `sleep_for` with the
default 2,304-point frame. They are superseded by the raw lines above; the
harness argument order is `--synthetic-load <port> <hz> <points> [deadline]`.

Raw output for this addendum: `2026-10-20-plan151-publish-rate-addendum.log`
(the four runs' full `[synthetic]` / `[perf]` / probe lines).

## The row

Row #151 **is** on `origin/main` (filed by #365, merged as `a99ecd4`) with the
status `TODO`, and this record leaves it there: the device-free half is done,
but the row's P3 item — the *browser's* frame age — still has the browser's
render loop unmeasured, and the vendor's content cadence (§1) and the
device-side rate distribution are owed a device run. `PLAN.md` is not touched
by this change.
