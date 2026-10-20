# PLAN #139 — the pacing wait is now its own [perf] segment

**Date**: 2026-10-19
**Target**: `tools/scan_server/main.cpp` + `perf_window.h` (device-free run)
**Command**: `scan_server --synthetic 19139` (12 s)
**Exit**: 0

No device was used. This is the **mock/synthetic** evidence; the device-side
distribution is the hardware step (row #139 → `HW`, not `DONE`).

## The new line (verbatim, device-free)

```
[perf] window 5.0s n=128 rate=25.5/s points=2304.0 bytes=27698.4 | pose p50=0.00 p95=0.00 max=0.00 ms | getter p50=0.00 p95=0.00 max=0.00 ms | post p50=0.12 p95=0.24 max=0.35 ms | bcast p50=0.00 p95=0.00 max=0.00 ms | wait p50=39.35 p95=43.03 max=43.93 ms | overshoot p50=6.02 p95=9.70 max=10.60 ms | tick p50=39.57 p95=43.27 max=44.12 ms
```

### Field names (exact)

- the four historical segments are unchanged, in order: **`pose`**, **`getter`**,
  **`post`**, **`bcast`** (p50/p95/max, ms) — same indices (0–3), same meaning;
- **`wait`** — the pacing call (`CFRunLoopRunInMode` on macOS, the sleep
  elsewhere), p50/p95/max ms;
- **`overshoot`** — `wait − nominal_wait` (100 ms live, 33.333 ms synthetic),
  p50/p95/max ms; negative means the wait returned early;
- **`tick`** — the realized period (start-to-start), as before.

`wait + work ≈ tick` here: 39.35 + (0.00+0.00+0.12+0.00) ≈ 39.57 ms. On the
device the finding was work ≈ 1.5–2 ms against a 110–130 ms tick, so `wait`
should carry the remainder and `overshoot` should expose the 732/1373/6410 ms
spikes the four segments never moved.

## Device-free coverage

`perf_window_test` (ctest, not hardware-labelled) asserts: the four work
segments keep indices/names; `wait` is recorded separately; `overshoot =
wait − nominal` (including a negative case); `TakeSnapshot()` does not reset;
and the work sum stays the four segments. It fails if the wait is folded back
into the tick or dropped.

```
[perf_window_test] PASS: four work segments unchanged; wait recorded separately; overshoot = wait - nominal
```

## What this does not settle

Whether the overshoot is a synchronous **vendor callback** draining inside the
run-loop or OS scheduling. That needs the device-side distribution — the
hardware step for #139.

Raw output: `2026-10-19-plan139-pacing-wait.log`.
