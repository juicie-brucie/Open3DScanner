# Addendum — investigation request: the condition 4→5 tick-latency outlier in `2026-10-19-plan112-tick-latency-baseline.md`

Per `tests/hardware/README.md` ("Results are written to `../results/`, one
file per run, never edited in place"), this is a separate addendum, not an
edit to the original record.

**Attribution:** the hypothesis below is the user's own, relayed via the
coordinator — not this agent's analysis. This agent has pinned the exact
location in the raw log and gathered what's directly observable there, but
has **not** diagnosed or confirmed the hypothesis.

## The outlier

Original record flagged a severe single-tick spike at the condition 4→5
boundary: `[perf] window ... max=6410.48 ms`. Exact location, from
`2026-10-19-plan112-tick-latency-baseline.raw.log`:

```
16662:[perf] window 5.1s n=30 rate=2.6/s points=19815.9 bytes=238023.3 | pose p50=0.00 p95=0.01 max=0.01 ms | getter p50=0.00 p95=0.00 max=0.18 ms | post p50=1.68 p95=2.80 max=2.81 ms | bcast p50=0.11 p95=0.49 max=0.76 ms | tick p50=121.94 p95=1141.74 max=6410.48 ms
```

**Correction to the original record's framing:** the original record
described this as occurring "right at the condition 4→5 transition." The
two nearest timestamped SDK log lines bracketing it are:

```
16462:[10/19 19:55:13.284848][warning][18661625][Pipeline.cpp:310] Wait for frame timeout, you can try to increase the wait time! current timeout=100 [**43 logs in 6102ms, last: 19:55:12.260724**]
...
17572:[10/19 19:55:25.380179][warning][18653818][Pipeline.cpp:310] Wait for frame timeout, you can try to increase the wait time! current timeout=100 [**5 logs in 12095ms**]
```

So the outlier's perf-window line (16662) falls **between 19:55:13.28 and
19:55:25.38** — i.e., inside condition 5's own 30s window
(19:55:04–19:55:34, "UI open, moved by hand"), not exactly at the
condition 4/5 boundary (condition 4 ended 19:55:00, condition 5 started
19:55:04). This narrows the window for whoever investigates.

**What's directly observable in that line range (16462–16900), checked but
not interpreted:**
- All 143 `[worker][data] FRAME_READY ... tracking_status=0 (SUCCESS)`
  lines in that range show `tracking_status=0` — no tracking-status
  discontinuity is visible in this stretch.
- No other warning/error lines appear in this range besides the two
  "Wait for frame timeout" lines quoted above (which print throughout the
  run as routine rate-limited background noise; no unique error at this
  moment was found).
- The perf line's own throughput fields in this window also dropped:
  `rate=2.6/s` (vs. the ~8–9/s typical elsewhere), `n=30` samples (vs. the
  usual ~44–45 per 5s window), `p95=1141.74ms` — consistent with one or a
  few individual ticks stalling hard while most others in the window ran
  normally.

## The user's hypothesis

The user thinks the scanner may have been moved unusually **fast** right
around the condition 4→5 transition — faster than the sustained "moved by
hand" motion used for the rest of condition 5 — and that this rapid
movement could explain the stall.

## Requested investigation (not performed here)

For whoever picks this up (recon/builder level): using the timestamp
bracket above (19:55:13.28–19:55:25.38, raw-log lines ~16462–17572) and the
exact outlier line (16662), look at what's observable in the logs and data
path at that point to see whether the evidence supports or rules out the
rapid-movement explanation — e.g.:
- Any IMU/pose-related log lines in that window (this agent did not search
  for those specifically).
- Frame drops / "data size does not match the expectation" warnings
  (`VideoSensor.cpp:455`) in that exact bracket vs. elsewhere in the run.
- Point-cloud discontinuities (sudden jumps in `color_cloud num_points` or
  similar) at the tick boundaries inside this window.
- Whether the specific stalled tick(s) correlate with anything in the
  vendor SDK's own device-side logging (GVCP/RPC lines) in that bracket.

## Reference

- Original record: [`2026-10-19-plan112-tick-latency-baseline.md`](./2026-10-19-plan112-tick-latency-baseline.md)
- Raw log: `2026-10-19-plan112-tick-latency-baseline.raw.log` (lines 16462–17572 for this bracket; line 16662 for the outlier's own perf-window line)
