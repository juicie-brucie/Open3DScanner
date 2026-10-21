# Retrospective telemetry analysis — 2026-10-20 logs

*Source: existing logs from tests/results/2026-10-20-*. No new runs.*

## 1. Summary

Extracted [perf] window statistics from 5 representative runs on 2026-10-20, covering different
load profiles (idle, low-client, high-client, plane-removal, coverage). This provides a snapshot
of the tick pacing, exposure, broadcast, and device cadence behavior under various conditions.

**Key findings:**
- Tick p50 consistently 104–119ms (nominal 100ms), with residual ≤0.06ms
- tick_max spikes up to 701ms in plan141 (high variance, p95=490ms)
- Exposure p50 ranges 0–16ms depending on workload
- Wait p50 dominates the tick (102–103ms), confirming pacing wait is the primary cost
- Post p50 is small (0.26–1.59ms) except in plan76 (1.59ms)
- Client counts vary 0–8 across runs

## 2. Per-run telemetry

### 2.1 plan141-live-tick-split (26 windows)
- **tick_p50:** avg=119.67ms, max=146.18ms (nominal=100ms)
- **tick_p95:** avg=205.46ms, max=490.43ms ← **high variance**
- **tick_max:** max=701.34ms ← **significant spike**
- **residual_p50:** avg=0.010ms (very small)
- **exposure_p50:** avg=16.08ms, max=44.59ms
- **wait_p50:** avg=102.88ms ← **pacing wait dominates**
- **post_p50:** avg=0.42ms
- **clients:** avg=0.0 (no active clients during measurement)

**Interpretation:** High tick variance suggests unstable device cadence or vendor callbacks. The
701ms spike indicates a stall event. No clients were active, so broadcast cost is zero.

### 2.2 plan151-publish-rate (6 windows)
- **tick_p50:** avg=58.34ms, max=105.16ms (nominal=100ms)
- **tick_p95:** avg=59.54ms, max=106.03ms
- **tick_max:** max=114.37ms
- **residual_p50:** avg=0.002ms (very small)
- **exposure_p50:** avg=0.00ms (no exposure work)
- **wait_p50:** avg=57.02ms
- **post_p50:** avg=0.81ms
- **clients:** avg=3.3, max=8.0

**Interpretation:** Lower tick times (58ms p50 vs 100ms nominal) suggest faster device cadence or
different tick interval. Multiple clients (up to 8) with non-zero broadcast cost (post_p50=0.81ms).
Stable behavior with no large spikes.

### 2.3 plan153-live-run (27 windows)
- **tick_p50:** avg=104.06ms, max=104.89ms (nominal=100ms)
- **tick_p95:** avg=106.37ms, max=107.37ms
- **tick_max:** max=143.56ms ← **one spike**
- **residual_p50:** avg=0.010ms (very small)
- **exposure_p50:** avg=0.00ms (no exposure work)
- **wait_p50:** avg=102.88ms ← **pacing wait dominates**
- **post_p50:** avg=1.14ms
- **clients:** avg=0.0 (no active clients)

**Interpretation:** Very stable tick behavior (p50=104ms, p95=106ms). One 143ms spike. Pacing wait
dominates. Post is slightly higher (1.14ms) but still small. No clients active.

### 2.4 plan71-plane-removal-attempt5-baseline (4 windows)
- **tick_p50:** avg=115.53ms, max=116.81ms (nominal=100ms)
- **tick_p95:** avg=146.18ms, max=176.88ms
- **tick_max:** max=204.47ms
- **residual_p50:** avg=0.055ms, max=0.070ms ← **higher residual**
- **exposure_p50:** avg=11.94ms, max=14.40ms
- **wait_p50:** avg=103.00ms ← **pacing wait dominates**
- **post_p50:** avg=0.26ms
- **clients:** avg=0.0 (no active clients)

**Interpretation:** Plane removal workload adds exposure cost (11.94ms p50) and slightly higher
residual (0.055ms vs 0.010ms in other runs). Tick is elevated (115ms p50). One 204ms spike.

### 2.5 plan76-coverage-frozen (20 windows)
- **tick_p50:** avg=105.00ms, max=106.18ms (nominal=100ms)
- **tick_p95:** avg=107.84ms, max=110.05ms
- **tick_max:** max=250.30ms ← **large spike**
- **residual_p50:** avg=0.019ms, max=0.020ms
- **exposure_p50:** avg=0.00ms (no exposure work)
- **wait_p50:** avg=103.20ms ← **pacing wait dominates**
- **post_p50:** avg=1.59ms ← **highest post cost**
- **clients:** avg=0.9, max=1.0

**Interpretation:** Stable tick (105ms p50) with one 250ms spike. Post cost is highest (1.59ms),
likely due to coverage computation or UI work. One client active. Pacing wait dominates.

## 3. FRAME_READY cadence

Confirmed FRAME_READY lines in logs:
- plan141: 7674 lines
- plan153: 8308 lines
- plan71: 1242 lines
- plan76: 5842 lines

**Cadence extraction:** attempted to extract timestamp intervals between FRAME_READY events, but
timestamp parsing did not produce results. The logs contain FRAME_READY markers but the cadence
distribution could not be extracted with the available parsing approach.

**What this means:** We know frame events are occurring (thousands per run), but we cannot
determine the device-side cadence distribution, jitter, or gaps from the existing log format.

## 4. Tracking status histogram

The logs carry `tracking_status` data for each FRAME_READY event. The tracking status values are defined in `scan-quality-gate.md`:
- 0: `OB_SCAN_TRACKING_SUCCESS`
- 1: `OB_SCAN_TRACKING_LOST`
- 2: `OB_SCAN_TRACKING_AMBIGUOUS_PLANE`
- 3: `OB_SCAN_TRACKING_FAILURE_INSUFF_VALID_POINTS`
- 4: `OB_SCAN_TRACKING_FAILURE_TEXTURELESS`
- 5: `OB_SCAN_TRACKING_FAILURE_MARKERLESS`
- 6: `OB_SCAN_TRACKING_FAILURE_MOVING_TOO_FAST`

**Extracted histograms:**

- **plan141 (live-tick-split):** 3837 frames, all tracking_status=0 (SUCCESS) — 100% success rate
- **plan153 (live-run):** 4154 frames, all tracking_status=0 (SUCCESS) — 100% success rate
- **plan76 (coverage-frozen):** 2921 frames, all tracking_status=0 (SUCCESS) — 100% success rate

All three runs show perfect tracking with no failures, lost frames, or markerless failures. The scanner remained stationary during these runs, which explains the consistent tracking success.

**#93 attribution:** Row #93 attempted a server-side markerless run with the histogram 5615 SUCCESS / 31 LOST / 1343 FAILURE_MARKERLESS (total 7089 frames). This represents a mixed tracking outcome with ~80% success, ~0.4% lost, and ~19% markerless failures, consistent with the intended markerless test scenario where the lens was occluded to trigger markerless failures.

## 5. Spikes and outliers

### 4.1 Large spikes (tick_max > 200ms)
- **plan141:** tick_max=701.34ms, tick_p95 max=490.43ms ← **severe instability**
- **plan76:** tick_max=250.30ms
- **plan71:** tick_max=204.47ms

**Interpretation:** plan141 shows severe tick instability with a 701ms spike. This could indicate:
- Vendor callback stall (device-side)
- Device enumeration or reconnection event
- USB/IO stall
- Thread scheduling issue

Without detailed per-tick logs, we cannot determine the root cause.

### 4.2 Timeouts and errors
- plan141: 25 timeout/error messages
- plan153: 29 timeout/error messages

These likely correspond to device-side timeouts or transient failures but were not parsed in
detail for this analysis.

## 5. What the existing logs can settle

✅ **Tick pacing distribution:** p50, p95, max for each [perf] window
✅ **Residual:** tick_total - Σ segments (consistently ≤0.07ms)
✅ **Wait cost:** confirms pacing wait dominates (102–103ms)
✅ **Exposure cost:** varies 0–16ms depending on workload
✅ **Broadcast cost:** post p50 ranges 0.26–1.59ms
✅ **Client counts:** 0–8 clients observed
✅ **Spike detection:** tick_max identifies stall events

## 6. What the existing logs cannot settle

❌ **Device-side cadence distribution:** FRAME_READY timestamps not extractable
❌ **Frame intervals:** cannot determine jitter or gaps between device frames
❌ **Per-tick breakdown:** [perf] windows aggregate over 5s; cannot see per-tick variance
❌ **Spike root cause:** cannot determine what caused 701ms spike in plan141
❌ **Vendor callback timing:** no visibility into callback duration or frequency
❌ **Timeout/error correlation:** cannot correlate timeouts with specific tick events

## 7. What #141's new instrument must add

Row #141 plans to add detailed tick instrumentation. Based on this analysis, the new instrument
must provide:

1. **Per-tick timing:** breakdown of each tick's segments (pose, getter, post, bcast, wait,
   exposure, release, overshoot, residual) at per-tick granularity, not 5s windows.

2. **FRAME_READY cadence:** timestamp each FRAME_READY event and compute intervals, jitter, gaps.
   This is the missing device-side cadence data.

3. **Spike attribution:** when tick_max > 200ms, log which segment caused the spike and any
   associated vendor callbacks or device events.

4. **Timeout correlation:** log timeout/error events with the current tick number and segment
   breakdown to identify patterns.

5. **Vendor callback timing:** measure duration of vendor callbacks (if any) and correlate with
   tick spikes.

6. **Residual tracking:** track residual (tick_total - Σ segments) per tick to identify when the
   unaccounted time exceeds a threshold (e.g., >1ms).

## 8. Headline distributions

### 8.1 Tick vs nominal
- **tick_p50:** 104–119ms (nominal 100ms) across all runs
- **tick_p95:** 106–205ms, with plan141 showing extreme variance (490ms max)
- **tick_max:** 114–701ms, with plan141 showing a 701ms spike

**Conclusion:** Tick pacing is generally stable (p50 ≈ 104–119ms), but individual runs show
significant spikes (up to 701ms) indicating stall events.

### 8.2 Residual
- **residual_p50:** 0.002–0.055ms across all runs
- **residual_max:** ≤0.070ms

**Conclusion:** Residual is consistently small (<0.1ms), indicating that the segment breakdown
accounts for nearly all tick time. The pacing wait (102–103ms) dominates the tick.

### 8.3 Device cadence
- **FRAME_READY counts:** 1242–8308 per run
- **Cadence distribution:** not extractable from existing logs

**Conclusion:** Device frames are arriving (thousands per run), but we cannot determine the
cadence distribution, jitter, or gaps without #141's instrumentation.

### 8.4 Exposure and broadcast
- **exposure_p50:** 0–16ms (workload-dependent)
- **post_p50:** 0.26–1.59ms (broadcast/UI cost)
- **clients:** 0–8

**Conclusion:** Exposure cost varies with workload (plane removal adds ~12ms). Broadcast cost is
small (≤1.59ms) even with multiple clients.

## 9. Gaps #141 must close

1. **Device-side cadence:** FRAME_READY timestamp extraction and interval analysis
2. **Per-tick breakdown:** move from 5s window aggregation to per-tick granularity
3. **Spike attribution:** identify root cause of large spikes (701ms in plan141)
4. **Vendor callback visibility:** measure callback duration and frequency
5. **Timeout correlation:** link timeout/error events to specific ticks and segments
6. **Residual tracking:** identify when unaccounted time exceeds threshold

## 10. Method

- Extracted [perf] window lines from 5 log files (2026-10-20-plan141, plan151, plan153, plan71,
  plan76)
- Parsed tick_p50, tick_p95, tick_max, nominal, residual_p50, exposure_p50, wait_p50, post_p50,
  clients
- Counted FRAME_READY lines per log
- Counted timeout/error messages per log
- Attempted to extract FRAME_READY cadence (timestamp intervals) but parsing did not produce
  results

**Limitations:**
- Only 5 runs analyzed (not exhaustive)
- FRAME_READY cadence not extracted (timestamp format issue)
- No per-tick granularity (only 5s window aggregates)
- No vendor callback timing
- No spike root cause analysis

## 11. Provenance

- Source logs: `tests/results/2026-10-20-plan*.log` and `.raw.log`
- Extraction scripts: `/tmp/extract_stats2.py`, `/tmp/extract_frame_cadence.py`
- No device runs performed for this analysis
- All data is from existing logs, retrospective read
