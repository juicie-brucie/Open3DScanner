# Pull Semantics — Row #143

*Analysis of #132 gap-free captures (2026-10-20). `[hw-confirmed: plan132-gapfree-captures]`*

## 1. The Question

What is the pull semantics of `ob_rpc_get_frame`? Does the client receive a stream of frames, or does it poll for the latest available frame? What is the relationship between the device's frame production rate and the client's consumption rate?

## 2. The Evidence — #132 Gap-Free Captures

The #132 hardware run captured complete RPC reply streams from both IR and depth interfaces post-power-cycle, with `pull_ms=0` (back-to-back polling, no artificial delay).

**Correction to #131 element naming:** The observed 11-element array has e1=0 for every reply and e2 carries the actual microsecond timestamps. This corrects the #131 element naming where e1 was named "the frame's µs timestamp". All timestamp references below refer to e2.

### 2.1 IR Interface (port 10013)

- **180 frames in 30 seconds** = **6.0 Hz effective rate**
- **0 empty replies** (180/180 data-bearing)
- **Monotonically increasing e2 timestamps** (0 non-increasing pairs)
- **Inter-frame delta**: min=101,645 µs, max=233,336 µs, **mean=166,870 µs**
- Every frame is Bin(2304000) = 2.3 MB (whole frame, from #174)

### 2.2 Depth Interface (port 10010)

- **200 frames in 30 seconds** = **6.67 Hz effective rate**
- **0 empty replies** (200/200 data-bearing)
- **Monotonically increasing e2 timestamps** (0 non-increasing pairs)
- **Inter-frame delta**: min=64,055 µs, max=167,291 µs, **mean=139,214 µs**
- 199 frames are Bin(2304000), 1 frame is Bin(1321920) (partial frame at msgid=914)

## 3. Fresh vs Latest vs Re-Sent

**Answer: Each `get_frame` reply returns the LATEST AVAILABLE frame, not a re-send.**

Evidence:
- e2 timestamps are **monotonically increasing** on both interfaces
- No duplicate timestamps observed
- Each reply carries a NEWER frame than the previous one
- The daemon does not queue frames for later delivery; it returns the most recent frame when polled

This is a **poll-and-respond** model, not a queue-drain or stream model.

## 4. Empty Reply Share

**Answer: 0% in the #132 capture.**

This contradicts the old model from #124/#131 (based on session4.pcapng), which claimed:
- ~41% empty replies at ~84 Hz polls
- Effective frame rate ~49.8 fr/s

**Why the discrepancy?**

The session4.pcapng capture had a BPF filter `less 16000` that dropped segments ≥16 KB, so it only captured TCP fragments, not complete RPC replies. The ~41% empty rate and ~84 Hz poll rate were artifacts of incomplete capture data, not actual protocol behavior.

The #132 capture has no BPF filter restrictions and captures complete RPC replies. The 0% empty rate indicates that when the client polls at ~6-7 Hz, there is always a new frame available.

## 5. Actual Poll Cadence

**Answer: The client polls at ~6-7 Hz, NOT at the device's 50 fps production rate.**

Key insight:
- The device profile claims 50 fps (20 ms per frame)
- But the client only receives frames at ~6-7 Hz (140-167 ms per frame)
- The client uses `pull_ms=0` (back-to-back polling, no artificial delay)
- The ~6-7 Hz rate is **NOT the device's frame production rate**
- It is the **client's poll rate limited by sequential RPC round-trips**

The client implementation (raptor_client) polls sequentially:
1. Send `get_frame` request
2. Wait for 2.3 MB response
3. Process response
4. Repeat

The RPC round-trip time (send + receive + process) is ~140-167 ms, which limits the effective poll rate to ~6-7 Hz.

**This is NOT a streaming model.** The client is not receiving a continuous stream of frames at 50 fps. It is polling at ~6-7 Hz and receiving the latest available frame each time.

## 6. Stream vs Sample

**Answer: This is a SAMPLE model, not a STREAM model.**

Evidence:
- The client polls sequentially (one in-flight RPC at a time)
- The daemon returns the latest available frame when polled
- There is no queueing, no batching, no streaming
- The client's poll rate (~6-7 Hz) is much lower than the device's production rate (~50 fps)
- The client is sampling the frame stream at its own poll rate

**The "stream" is actually a sequence of samples taken at the client's poll rate.**

The device produces frames at ~50 fps, but the client only samples at ~6-7 Hz. Between polls, the device produces ~7-8 frames, but the client only sees the latest one. The intermediate frames are discarded (or overwritten in the daemon's internal buffer).

## 7. What the Old Model Got Wrong

The old model from #124/#131 (based on session4.pcapng) claimed:
- ~12 KB bands requiring client-side assembly (falsified by #174)
- ~84 Hz polls with ~41% empty replies (falsified by #132)
- Effective frame rate ~49.8 fr/s (falsified by #132)

**Root cause:** The session4.pcapng capture had a BPF filter `less 16000` that dropped segments ≥16 KB. The capture only contained TCP fragments, not complete RPC replies. The analysis tools misinterpreted these fragments as the actual payload size and poll pattern.

**The reality:**
- The daemon sends whole 2.3 MB frames (not 12 KB bands)
- The client polls at ~6-7 Hz (not 84 Hz)
- There are 0% empty replies (not 41%)
- The effective frame rate is ~6-7 Hz (not 49.8 fr/s)

## 8. The Daemon's Internal Buffer

**Open question: Does the daemon have an internal frame buffer?**

The evidence suggests:
- The daemon produces frames at ~50 fps
- The client polls at ~6-7 Hz
- Between polls, the daemon produces ~7-8 frames
- The client only sees the latest frame (no queueing observed)

This implies the daemon has an internal buffer that holds the most recent frame (or a small ring buffer). When polled, it returns the latest frame and discards older frames.

**This is consistent with a "latest frame" model, not a "queue all frames" model.**

If the daemon queued all frames, the client would see a backlog of frames on the first poll (after a slow start), and the e2 timestamps would show the device's 50 fps production rate. Instead, the e2 timestamps show the client's ~6-7 Hz poll rate.

## 9. Implications for C2 (Phase B)

The pull model is simple:
1. Client polls `ob_rpc_get_frame` at its own cadence
2. Daemon returns the latest available frame
3. No queueing, no batching, no streaming
4. The client's poll rate is limited by RPC round-trip time, not by the device's frame production rate

**For C2:** The client should poll at a rate that balances:
- Frame freshness (poll more often = more recent frames)
- Network/CPU overhead (poll less often = less overhead)
- Application requirements (some applications need 50 fps, others can tolerate 6-7 Hz)

The raptor_client implementation polls at ~6-7 Hz (limited by sequential RPC round-trips). A production client might use asynchronous polling (multiple in-flight RPCs) to achieve higher effective frame rates.

## 10. Open Questions

### 10.1 The Daemon's Buffer Size

Does the daemon have a ring buffer of N frames, or does it only keep the latest frame?

**Test:** Start the stream, wait 1 second (during which the daemon produces ~50 frames), then poll once. If the daemon only keeps the latest frame, the first poll will return the most recent frame (e2 timestamp ≈ start_time + 1s). If the daemon queues all frames, the first poll will return the oldest frame (e2 timestamp ≈ start_time), and subsequent polls will drain the queue.

### 10.2 The Partial Frame (Bin(1321920))

Why did the depth interface return one partial frame (688.5 rows) at msgid=914?

This is likely a startup artifact (the first frame after `start_stream` is not yet complete). But it could also be a timing edge case (the daemon returns a frame that was in the middle of being captured).

**Test:** Capture multiple start/stop cycles and check if the partial frame always appears at the same position (e.g., first frame, 152nd frame, etc.).

### 10.3 Asynchronous Polling

Can the client achieve higher effective frame rates by using asynchronous polling (multiple in-flight RPCs)?

**Test:** Modify raptor_client to issue N concurrent `get_frame` requests and measure the effective frame rate. If the daemon supports concurrent requests, the effective rate should increase to min(N × poll_rate, device_production_rate).

## 11. Summary

| Question | Answer | Confidence |
|----------|--------|------------|
| Fresh vs latest vs re-sent? | **Latest available** (monotonically increasing timestamps) | hw-confirmed |
| Empty reply share? | **0%** (in #132 capture) | hw-confirmed |
| Actual poll cadence? | **~6-7 Hz** (limited by RPC round-trip time) | hw-confirmed |
| Stream vs sample? | **Sample** (client polls at its own rate, not a continuous stream) | hw-confirmed |
| Device production rate? | **~50 fps** (per profile, not directly measured) | inferred |
| Daemon buffer model? | **Latest-frame** (not queue-all) | inferred |

---

**Wall**: `docs/recon/**`
**Status**: `[hw-confirmed: plan132-gapfree-captures]` — verified by gap-free hardware captures
**Supersedes**: #124/#131 pull model (based on incomplete capture data)
**Related**: #174 (frame delivery), #126 (pixel scale)
