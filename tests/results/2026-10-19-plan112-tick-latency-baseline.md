# PLAN #112 — live tick-latency baseline (Mac half of #115's cross-machine comparison) — clean 6/6-condition run

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server (build-tree copy (fixed order: build/tools/<target>/<target>))
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (18:41:20) — refreshed only on an app build (#107)
- Preflight: pwd OK; no conflicting process; freshness: binary 18:41:20 >= newest source main.cpp 14:59:05
- Interposer: expect line 2: `[vulkan_disable_interpose] loaded` (scan_server sets DYLD_INSERT_LIBRARIES itself)
- Run start: 2026-10-19 19:51:48
- Device: Creality Raptor Pro, USB3.0 — attached, a scan target/scene positioned in front of it for real tracking data. No other SDK client except as described per condition.

**This record supersedes all earlier same-day attempts** at this baseline
(a stray-browser-contaminated no-client attempt, a setup-time vendor-SDK
crash before any condition started, and a broken-pipe-aborted partial
rerun) — those were discarded per instruction and never written here. This
is the first complete, clean 6-condition run.

## Commands

```
scripts/hw-preflight.sh scan_server
./build/tools/scan_server/scan_server --live 8787
```

Conditions 1–3 (no client): confirmed genuinely no-client throughout via
`lsof -i :8787` (only the `LISTEN` socket, no `ESTABLISHED` connection)
except during condition 3's deliberate ~20s transient WS connection.
Condition 3's pause/resume was sent via a minimal, dependency-free WS
script (connects, drains incoming frames while held so the server's send
buffer never backs up and force-closes the connection — the bug in an
earlier attempt today — sends `{"cmd":"pause"}`, drains again, sends
`{"cmd":"start"}`, disconnects).

Conditions 4–6 (UI open): a real browser pane connected to
`127.0.0.1:8787` and stayed connected (`clients=1`) throughout. Condition
6's pause/resume was **intended** to go through the browser UI (user
clicking Pause then Resume), but the click did not register — `paused=0`
continuously through the entire announced 30s window and beyond, confirmed
by scanning the tick log well past the window boundary. Per the
pre-agreed fallback, the same transient WS script was then used instead
(with the browser client still connected throughout, `clients` briefly
`2`, then back to `1`). **Condition 6 therefore used the WS-script
mechanism, not a real UI click** — the user accepted this as equivalent
("same underlying pause/resume mechanism either way") rather than redoing
it, so it's recorded as the valid measurement for condition 6 with this
caveat flagged.

## Result

- Exit code: `0` (`[launcher] Worker exited normally with code 0.`, clean `SIGINT` teardown through the normal pause → `get_realtime_model_mesh` → stop → `obscan_context_destroy` sequence)
- Verdict: **PASS — clean 6/6-condition run.**
- Raw log: `2026-10-19-plan112-tick-latency-baseline.raw.log` (same directory; full startup through teardown)
- End-of-run tracking histogram: `tracking_status samples=8119 histogram: SUCCESS=7904 LOST=215` — real tracking data throughout (unlike an earlier same-day discarded attempt with no scene positioned, which showed `FAILURE_MARKERLESS` almost exclusively).
- `Frame protocol V1 counters: cloud_frames=2349 full_detail_chunks=317`
- End-of-run summary line: `[perf] summary ticks=2522 mean_points=16494.4 mean_bytes=198152.4 pose_mean=0.00ms pose_max=0.09ms getter_mean=0.00ms getter_max=0.18ms post_mean=1.18ms post_max=5.05ms bcast_mean=0.05ms bcast_max=0.96ms`

## The 6 conditions

Window boundaries are derived by matching each condition's announced
wall-clock start/end against the `[perf] window` line's 5-second cadence
from the context-create timestamp (`19:51:54.77`); ±5s edge uncertainty
where a boundary falls mid-window.

### Condition 1 — (a) no client connected, STILL
- Window: 19:52:15–19:52:45
- tick p50: 111.52–112.88 ms; p95: 114.71–121.07 ms; max: 116.12–130.16 ms
- points/frame: 9526.9→14908.8 (still climbing from the scan's cold start)
- bytes/frame: 114559.7→179133.8

### Condition 2 — (a) no client connected, MOVED BY HAND
- Window: 19:52:48–19:53:19
- tick p50: 112.65–114.95 ms; p95: 116.87–123.61 ms; max: 121.14–163.77 ms
- points/frame: 17942.9→19843.1 (reaching near-steady-state)
- bytes/frame: 215552.2→238345.6

### Condition 3 — (a) no client connected, PAUSED-RESUMED (genuine — confirmed via tick log)
- Window: 19:53:24–19:53:45
- Pre-pause: tick p50=113.12, p95=124.45, max=154.12, points=18871.6
- **Paused** (`clients=1, paused=1`): tick p50=102.10, p95=104.85, max=104.99, **points=0.0, bytes=37.4** — confirms the pause genuinely stops cloud production, not just a flag
- Resuming: tick p50=103.08, p95=104.97, max=138.27, points=1210.8 (recovering)
- Recovered: tick p50=113.79, p95=129.21, max=137.49, points=19808.3

### Condition 4 — (b) UI open, STILL
- Window: 19:54:30–19:55:00
- tick p50: 114.67–116.29 ms (one window, mid-condition, p95=200.28/max=433.66 — a single outlier tick, not attributable to any user action within this still condition); points/frame steady ~19693–19708; bytes/frame ~236550–236725
- **Note:** a severe outlier window immediately following this condition's boundary (rate dropped to 2.6/s, tick p95=1141.74ms, max=6410.48ms) coincides with the condition 4→5 transition — likely the moment-of or near the browser's own connection/render activity, not attributed to either condition's data since it straddles the boundary.

### Condition 5 — (b) UI open, MOVED BY HAND
- Window: 19:55:04–19:55:34
- tick p50: 114.69–119.92 ms; p95: 120.39–326.20 ms; max: 127.72–1373.16 ms (one window shows a 1373ms single-tick spike — a visible outlier during this condition, not explained further here)
- points/frame: ~19778–19904; bytes/frame: ~237568–239085

### Condition 6 — (b) UI open, PAUSED-RESUMED (via WS-script fallback — browser UI click did not register)
- Announced window: 19:55:41–19:56:11 — **no pause occurred** in this window (`paused=0` throughout and beyond); this portion's data (tick p50 114.30–117.08, p95 118.32–165.98, max 121.81–420.61, points ~19741–19882) is effectively a second UI-open/still segment, not a paused/resumed one.
- **Fallback pause/resume**, 19:56:36–19:56:51, via the WS script with the browser client still connected (`clients` 1→2→1):
  - Pre-pause: tick p50=114.75, p95=130.25, max=133.95, points=19791.9
  - **Paused** (`clients=2, paused=1`): tick p50=102.92, p95=110.96, max=204.50, **points=0.0, bytes=38.3**
  - Resuming: tick p50=104.31, p95=187.87, max=361.48, points=6444.7 (recovering)
  - Recovered: tick p50=114.47–114.81, p95=116.64–139.66, max=128.19–185.98, points=19801.4–19810.0

## Crash sweep

`scripts/hw-postflight.sh 2026-10-19-plan112-tick-latency-baseline '2026-10-19 19:51:48' scan_server`:

> hw-postflight: no crash report for 'scan_server' at/after 2026-10-19 19:51:48 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.

Independently corroborated by the raw log's single continuous process from
startup to `Worker exited normally with code 0`, and `pgrep -x scan_server`
returning empty immediately after teardown.

## Open questions

- Condition 6's pause/resume used the WS-script fallback rather than a real
  browser-UI click (the click didn't register in time); the user accepted
  this as equivalent since the server-side mechanism and effect are
  identical either way. Is that acceptable for #112/#115's comparison, or
  does the comparison specifically need a UI-click-sourced measurement?
- Two visible single-tick outliers (condition 4→5 boundary: max=6410ms;
  condition 5: max=1373ms) are reported as observed, not diagnosed. Are
  these expected background noise (GC/compaction, a vendor SDK stall) or
  worth a closer look?
- No HUD browser-side `dataFps`/`renderFps` numbers were captured for
  conditions 4–6 — those are painted entirely client-side in the browser
  with no server-exposed endpoint, and no headless-browser tool was
  available to this agent. The server's own `rate=` field (~7–9/s
  throughout) is the closest available proxy.

## Board

- Row move I would propose for **#112**: ready to move from `HW` →
  `DONE` — all 6 conditions have valid data for the first time, with the
  one caveat above (condition 6's pause/resume source) flagged rather than
  hidden. If #115's cross-machine comparison needs a UI-click-sourced
  condition 6 specifically, that's a narrower ask than redoing this whole
  baseline.
