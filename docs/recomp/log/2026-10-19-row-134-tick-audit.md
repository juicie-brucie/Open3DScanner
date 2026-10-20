- 2026-10-19 — **#134 tick-latency audit** (recon-9; the session hit its step cap **after** the analysis, so this entry is
  transcribed from its report, not rewritten). **Headline: no code cause — the "improvement" is an instrument mismatch.**
  - The prior "tick numbers" were the **HUD `data` fps** (`ui/app.js:464`), a **client-side** rate that drops frames when the
    renderer is behind (`ui/app.js:293`, `if (framePending) return;`); `PLAN` row 121's "125–500 ms per tick" is `1000/fps` derived
    from that. The server-side instrument (`[perf] window`, `tools/scan_server/perf_window.h`, a single commit `ba33c54`) had **never
    produced a prior tick number** — so the "before" and "after" are different instruments on opposite sides of the wire.
  - The tick is **sleep-bound**: `kLiveTickIntervalMs = 100` + `CFRunLoopRunInMode(..., 0.1)`, unchanged since `4149299`
    (2026-10-16) → a ~100 ms floor; measured work ~1.23 ms/tick; p50 111–120 ms is 11–20 ms **above the floor**.
  - **Ruled out**: the frame path (`obscan_scan_get_model_point_color_clouds` — plural, `a5a78e7`; decimation `9cd0854`/`27947d3`;
    post 0.04→1.75 ms as points grew 861→19 900, never setting the tick), broadcast (0.05 ms mean = 0.04 %), the vendor (same dylib
    `edb0ef8bc230…`, SDK 3.5.3.260921, built 2026-09-03 — predates every run), and the environment (real tracking makes the tick
    *slower*, not faster).
  - **Variance**: n = 1 with the instrument; within-run window p50 111.52→121.94 ms (**9 %**), so an 11 % "gain" is not
    distinguishable from noise.
  - **New finding**: **~9.6 ms/tick sits outside all four timed segments** — the untimed vendor calls
    `obscan_scan_config_combined_exposure_and_laser_current` (re-asserted every tick; `main.cpp:1027` sets `auto_exposure = false`,
    `:1129`) and `obscan_scan_model_clouds_release` (`main.cpp:1272`), plus `BuildStatusJson` / the 50-tick `printf` and
    `RecordFrame` (a no-op without `--record`). See `scan-live-data.md` §20.
  - **What would settle it**: the HUD `data`/`render` capture (the 4–6 re-run already planned — if it reads ~8.8 fps, the client
    finally kept up, the server did not speed up), n ≥ 5 repeats, a scene A/B (0 vs ~19.7 k points), two more timed segments, or
    lowering `kLiveTickIntervalMs` (that is **#113**'s cadence/margin question, not a latency fix).
