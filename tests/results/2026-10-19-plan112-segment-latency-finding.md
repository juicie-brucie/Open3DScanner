# PLAN #112 — per-segment latency vs tick latency: the tick is the pacing wait, and it is uninstrumented

- Date: 2026-10-19
- Provenance: the hw session's own chat report, supplied by the operator and transcribed here **verbatim** — the
  session held its file edits, so this text did not previously exist in `tests/results/`. It is an addendum to
  `2026-10-19-plan112-tick-latency-baseline.md` and `2026-10-19-plan112-conditions-4-6-hud-capture.md`, not an edit
  to either.
- Scope: the four-segment `[perf]` data for the moments already identified, checked directly — no new run, no
  device work.
- Raw evidence: `2026-10-19-plan112-conditions-4-6-hud-capture.raw.log` (the segment windows and both pause
  clusters cited here) and `2026-10-19-plan112-tick-latency-baseline.raw.log` (the baseline's 1373 ms and
  6410 ms spikes). Line numbers are against those two files.

#112 — Per-segment latency breakdown vs. tick-level latency
(Reported in chat only; not yet written to any tests/results/ file, per instruction to hold file edits.)

QUESTION ASKED
#112's actual deliverable is a tick-latency baseline using #111's instrumentation,
which breaks each tick into FOUR segments:
  1. pose
  2. the publish-getter (the vendor call)
  3. our own post-processing + encode
  4. the broadcast
The point of #111/#112 was to show WHERE in the pipeline latency originates, not
just report aggregate tick p50/p95/max. Everything done in this session up to this
point (visual frame sweeps, point-cloud propagation, IMU recon) was useful but
tangential to that core question. This was a direct check of the per-segment data
for the moments already identified as points of interest.

SHORT ANSWER
The per-segment data exists and IS being logged, but it does NOT explain the
tick-level spikes. None of the four instrumented segments show anything unusual
during the 732ms spike or either pause — the latency is happening in an
uninstrumented fifth part of the loop (the inter-tick pacing wait).

1. THE 732ms SPIKE (line 9104, the window containing frame 63's dip)

pose p50=0.00 p95=0.00 max=0.01 ms | getter p50=0.00 p95=0.01 max=0.06 ms |
post p50=0.76 p95=1.09 max=1.21 ms | bcast p50=0.10 p95=0.25 max=0.30 ms |
tick p50=119.60 p95=224.05 max=732.71 ms

All four segments are trivially small — none comes anywhere close to 732ms.
The largest of the four (post, our processing/encode) peaks at 1.21ms.
The gap between tick max=732.71ms and the segment sum (~1.3ms max) is
roughly 560x. Whatever caused this spike happened entirely outside
pose/getter/post/bcast.

2. THE PAUSE/RESUME CLUSTERS

Neither shows a spike in any segment either. Windows bracketing both
Cluster A and Cluster B pauses:

Cluster A: pose max=0.01 | getter max=0.00 | post max=2.74 | bcast max=1.43 | tick max=373.27 ms
Cluster B: pose max=0.04 | getter max=0.01 | post max=4.10 | bcast max=0.39 | tick max=192.36 ms

Segments stay normal through both. This makes sense mechanically once you
look at the code: pausing skips the getter call and the post/encode work
entirely (nothing to fetch or process), so pause shows up as LESS work, not
a slow segment — it's invisible to this instrumentation rather than
attributable to the getter as originally hypothesized.

3. NORMAL STEADY-STATE — the real finding

pose p50=0.00 ms | getter p50=0.00 ms | post p50≈1.5-1.7 ms | bcast p50≈0.1 ms |
tick p50≈110-130 ms

The four segments sum to roughly 1.5-2ms at p50, while tick p50 runs
110-130ms — a ~70-100x gap, present in EVERY SINGLE WINDOW checked, not
just the spikes.

SOURCE TRACE (tools/scan_server/main.cpp)
The live loop's tick measurement:
  - tick_perf.BeginTick() at line 1076
  - tick_perf.EndTick() at line 1290
...wraps the inter-tick pacing call in between:
  - CFRunLoopRunInMode(kCFRunLoopDefaultMode, kLiveTickIntervalMs/1000.0, false)
    at line 1097, nominally ~100ms

So "tick" is measuring: pose + getter + post + bcast + the ~100ms pacing wait,
and the pacing wait is the dominant component by two orders of magnitude,
even in the normal case.

WHAT THIS MEANS FOR #112
The per-segment breakdown says the vendor getter call, our post-processing,
and the broadcast are NOT where tick-level latency originates — not in
steady state, and not during the spikes. The dominant cost, and the entire
source of the spike magnitude, is sitting inside the pacing/wait call itself
(CFRunLoopRunInMode), which #111's instrumentation doesn't break down any
further.

That's either:
  (a) working as intended — the loop is supposed to wait ~100ms between
      ticks, and tick p50≈110-130ms is close to that target, OR
  (b) the spikes (732ms here, and the baseline run's 1373ms/6410ms) represent
      that wait overshooting its nominal 100ms by a lot, for reasons
      invisible to this instrumentation (OS scheduling, a vendor callback
      firing synchronously inside the run-loop drain, something else).

The hw-test agent explicitly could not distinguish (a) from (b) using the
logs alone, and flagged this rather than diagnosing it further — that's a
recon/builder-level question.

STATUS
Not yet folded into tests/results/2026-10-19-plan112-conditions-4-6-hud-capture.md
or any other record. Decide whether this becomes its own addendum (it's a
distinct, higher-priority finding than the visual/geometry work) or gets
worked into the existing one.
