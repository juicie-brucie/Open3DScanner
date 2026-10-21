# PLAN #141 — tick instrument v2 (schema stamp + tick_overshoot) — PASS

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (12:53:07) — refreshed only on an app build (#107)
- Preflight: pwd OK; no conflicting process; daemon: RPCServer(76444,) running; freshness: binary 12:53:07 >= newest source main.cpp 12:52:55
- Interposer: expect line 2: `[vulkan_disable_interpose] loaded` — present
- Run start: 2026-10-21 12:56:49
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`; scan_server first access since prior session ended; count=1 first probe

## Command

```
./build/tools/scan_server/scan_server --live 8787
```

Run ~35 s; killed with SIGTERM (parent exit → `[worker] parent exited`).

## Result

- Exit code: SIGKILL on worker (parent exited; worker began clean teardown; `obscan_scan_pause -> OB_SCAN_SUCCESS` logged)
- Verdict: **PASS**
- Raw log: `2026-10-21-plan141-tick-instrument-v2.raw.log` (same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` — present, line 2

## New format fields (PR #438)

The `[perf] window` line now carries `unit=ms instrument=TickPerf` (schema stamp) and a
`tick_overshoot` field distinct from the existing `overshoot`:

- `overshoot` = wait − nominal (the pacing call's own excess over the 100 ms budget)
- `tick_overshoot` = tick − nominal (the whole tick's excess, including work segments)

Sample window (window 1):
```
[perf] window 5.0s clock=steady_clock unit=ms instrument=TickPerf n=49 rate=9.7/s
  points=160.8 bytes=2171.8
  wait p50=102.77 p95=105.03 max=105.08 ms
  overshoot p50=2.77 p95=5.03 max=5.08 ms
  tick_overshoot p50=2.80 p95=5.07 max=5.12 ms
  tick p50=102.80 p95=105.07 max=105.11 ms
  nominal=100.00 ms
```

## Checks

| # | Check | Result |
|---|---|---|
| 1 | Device enumerated: `count=1` first probe | **[PASS]** `obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1` |
| 2 | Scan started cleanly | **[PASS]** `obscan_scan_start -> OB_SCAN_SUCCESS (0)` |
| 3 | Schema stamp present in [perf] window | **[PASS]** `unit=ms instrument=TickPerf` in all 6 windows |
| 4 | `tick_overshoot` field present and distinct from `overshoot` | **[PASS]** `tick_overshoot p50=2.80–3.02` vs `overshoot p50=2.77–3.24` across 6 windows |
| 5 | `overshoot` = wait − nominal relation holds | **[PASS]** `wait p50=102.77`, `nominal=100.00`, `overshoot p50=2.77` (window 1) |
| 6 | `tick_overshoot` = tick − nominal relation holds | **[PASS]** `tick p50=102.80`, `nominal=100.00`, `tick_overshoot p50=2.80` (window 1) |
| 7 | `residual` present and small | **[PASS]** `residual p50=0.01–0.02 ms` across all windows |
| 8 | Clean teardown with scan_pause | **[PASS]** `obscan_scan_pause -> OB_SCAN_SUCCESS (0)` |
| 9 | No crash | **[PASS]** postflight: no crash report |

## Device-side tick distribution (6 windows, n=49 ticks each)

| Field | p50 range | p95 range | max |
|---|---|---|---|
| wait | 102.41–103.24 ms | 104.50–105.10 ms | 107.64 ms |
| overshoot (wait−nom) | 2.41–3.24 ms | 4.50–5.10 ms | 7.64 ms |
| tick_overshoot (tick−nom) | 2.75–3.21 ms | 4.57–5.22 ms | 7.72 ms |
| tick | 102.75–103.21 ms | 104.57–105.22 ms | 107.72 ms |
| residual | 0.01–0.02 ms | 0.05–0.10 ms | 0.29 ms |
| exposure applies | 0 (all windows) | | |
| broadcast sends | 0 (clients=0.0) | | |

Session summary: `ticks=295 mean_points=595.2 tick_overshoot_mean=2.68ms tick_overshoot_max=7.72ms overshoot_mean=2.63ms overshoot_max=7.64ms`

## Crash sweep

`scripts/hw-postflight.sh plan141-tick-instrument-v2 '2026-10-21 12:56:49' scan_server` →
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-21 12:56:49 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

## Open questions

- `tick_overshoot` and `overshoot` are nearly equal in this run (clients=0.0, no broadcast work). How much does `tick_overshoot` exceed `overshoot` when broadcast work is non-zero (i.e. when UI clients are connected)?
- The `exposure applies` count is 0 across all 295 ticks. Is the first-apply (before scan_start) counted by a different mechanism, or does the counter only measure mid-session re-applies?

## Board

- Row move: #141 `HW` → `DONE` — the schema stamp (`unit=ms instrument=TickPerf`) and `tick_overshoot` field from PR #438 are confirmed present and arithmetically correct on real hardware across 295 device ticks.
