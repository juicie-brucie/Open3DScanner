# PLAN #71 — plane-removal toggle observation, re-run attempt 5 (static-scene A/B, metres/correct-metric)

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: `efe9253` (Merge pull request #369 from juicie-brucie/dispatch/board-154)
- Target: build/tools/scan_server/scan_server (build-tree copy)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (mtime 01:11:42, refreshed only on an app build per #107)
- Preflight: pwd OK; no conflicting process; vendor RPCServer daemon running; freshness: binary 2026-10-20 01:11:42 >= newest source main.cpp 01:11:38
- Interposer: `[vulkan_disable_interpose] loaded` expected/confirmed, line 2 of output, both runs
- Run start: 2026-10-20 01:55:32
- Device: Creality Raptor Pro, serial `CR0886100SN`, USB3.0, daemon running, device enumerated (`count=1`, both runs)
- **One-SDK-user confirmed**: `pgrep -x scan_server`/`raptor_client` empty before each run; no conflicting process at any point.
- **Hard precondition**: user confirmed before this run that the scanner and scene would stay completely untouched across both runs (not moved, not bumped, not repositioned). This agent did not and could not independently verify that physically — the extents comparison below is the closest check available.

## Commands

```
./build/tools/scan_server/scan_server --live 8787 --record ~/captures/plan71-a5-baseline.rec
OPENSCANNER_PLANE_REMOVE=1 ./build/tools/scan_server/scan_server --live 8787 --record ~/captures/plan71-a5-enabled.rec
```

**Note on duration:** `--live`'s default session length is 300s
(`g_live_session_seconds`) when no positional duration is given — the
dispatch's commands don't specify one, so each run was started, left to
stream for ~16s (device enumerated and confirmed tracking within the
first ~5s of each), then stopped with a single `SIGINT` (the tool's own
documented "Press Ctrl+C to stop early" path — normal pause/stop/destroy
teardown, not a kill), landing each run at ~20s total as the dispatch
intended. Both runs' teardown completed cleanly (`Worker exited normally
with code 0`).

## Result

- Exit code: `0` for both runs (clean `SIGINT` → normal teardown)
- `tracking_status`: baseline `samples=621, SUCCESS=621` (100%); enabled
  `samples=583, SUCCESS=583` (100%)
- `Frame protocol V1 counters`: baseline `cloud_frames=177
  full_detail_chunks=2`; enabled `cloud_frames=161 full_detail_chunks=2`
- `obscan_scan_plane_remove(true) -> OB_SCAN_SUCCESS (0)` confirmed fired
  in the enabled run's log (not present in baseline's, as expected)
- Raw logs: `2026-10-20-plan71-plane-removal-attempt5-baseline.log`,
  `2026-10-20-plan71-plane-removal-attempt5-enabled.log` (same directory)
- Recordings (outside the repo, per convention):
  - `~/captures/plan71-a5-baseline.rec` — 15,004,828 bytes — sha256
    `d1fbdeb80a2aca3ed56bd601ad83605103d9465b116040f3203c108c0943d985`
  - `~/captures/plan71-a5-enabled.rec` — 9,976,704 bytes — sha256
    `5d8f383e8bbbf33b0cb24c716d1aa26ad29f16bb97af63a21666c632cfd298c3`

## The per-tick point-count series (the metric #71 actually calls for)

Both runs print a tick line every 50 ticks; each run reached tick 150
before `SIGINT`. `color_cloud num_points` (NOT `our_plane_removal`,
per attempt4's correction) at each sampled tick, and the **per-tick-window
increment** between samples:

| Tick | Baseline `num_points` | Baseline increment | Enabled `num_points` | Enabled increment |
| --- | --- | --- | --- | --- |
| 0 | 0 | — | 0 | — |
| 50 | 2,517 | **+2,517** | 5,282 | **+5,282** |
| 100 | 9,533 | **+7,016** | 5,444 | **+162** |
| 150 | 9,661 | **+128** | 5,550 | **+106** |

## Scene extents, with and without (attempt-4's parser, reused unmodified)

```
baseline: frames=355 cloud_frames=179 total_points_summed=1247517 max_points_in_single_frame=20000
  x: [-0.1859, 0.0848]   y: [-0.1207, 0.0725]   z: [0.2162, 0.5003]

enabled:  frames=323 cloud_frames=163 total_points_summed=828768  max_points_in_single_frame=20000
  x: [-0.1853, 0.0853]   y: [-0.1201, 0.0727]   z: [0.2161, 0.4993]
```

**The two runs' extents are nearly identical** (all six bounds agree to
within ~0.001–0.002 m) — unlike attempt4, where the two runs' extents and
point-count trajectories diverged wildly. This is consistent with (does
not prove, but is consistent with) the scene genuinely having been held
static this time, which is the precondition this run depended on.

## Variability check — is the baseline-vs-enabled difference bigger than noise within one run?

Comparing windows **within the baseline run alone** (tick50→100 vs
tick100→150): increments of **+7,016** and **+128** — a ~55x swing
between two consecutive windows of the exact same run, same toggle state,
same (held-static) scene. That is the internal/run-to-run variability
baseline for this measurement.

Against that baseline-internal variability:
- **tick0→50**: enabled's increment (+5,282) is actually *larger* than
  baseline's (+2,517) — the opposite direction from what plane-removal
  reducing published points would predict.
- **tick50→100**: enabled's increment (+162) is much smaller than
  baseline's (+7,016) — in the expected direction, but baseline's *own*
  very next window (tick100→150, +128) is nearly as small as this, using
  the same (off) toggle state. So a +7,016-vs-+162 gap exists between the
  two runs here, but baseline alone produces a comparably small increment
  one window later with nothing changed.
- **tick100→150**: enabled (+106) and baseline (+128) are close to each
  other — no meaningful difference in this window.

**The baseline-vs-enabled difference is not clearly larger than the
within-baseline window-to-window variability already observed** — the
one window where enabled is dramatically lower (tick50→100) is matched by
baseline's own next window dropping to a similarly low value with the
toggle untouched, and the first window actually goes the wrong direction.

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Interposer loaded, both runs | **[PASS]** |
| 2 | No conflicting process before either run | **[PASS]** |
| 3 | Device enumerated (`count=1`), both runs | **[PASS]** |
| 4 | `obscan_scan_plane_remove(true)` fires in the enabled run | **[PASS]** |
| 5 | Clean process exit, both runs | **[PASS]** exit code 0 |
| 6 | No crash, either run | **[PASS]** — see crash sweep below |
| 7 | Scene held static (extents agree across runs) | **[PASS]** — all six bounds within ~0.002 m |
| 8 | Point-count-increment difference exceeds within-run variability | **[FAIL]** — see variability check above |

## Verdict

**NOT SUPPORTED.** The toggle (`obscan_scan_plane_remove(true)`)
mechanically fires and succeeds (confirmed in the log), and this run's
extents evidence is consistent with the scene having actually been held
static this time (unlike attempts 3/4) — so this is **not** a confounded
run. But the per-tick point-count increments do not show a difference
attributable to the toggle: one window goes the wrong direction (enabled
higher than baseline), another shows enabled much lower but by an amount
baseline itself reproduces one window later with the toggle untouched,
and the final window shows no meaningful difference at all. The
observed baseline-vs-enabled gap does not exceed the variability already
present within a single (toggle-constant) run, so no point-count-reduction
effect can be read out of this data with confidence.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-20-plan71-plane-removal-attempt5 '2026-10-20 01:55:32' scan_server`:

> hw-postflight: no crash report for 'scan_server' at/after 2026-10-20 01:55:32 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.

## Open questions

- Why does `color_cloud num_points` swing by ~55x between two consecutive
  5-second windows of the *same* run with nothing changed (baseline's
  tick50→100 vs tick100→150)? This agent did not investigate the
  mechanism — it's the vendor's own accumulated-model getter
  (`obscan_scan_get_model_point_color_clouds`), and this large an
  intra-run swing on a genuinely static scene was not expected going in.
  This variability is itself the reason this run can't settle #71 either
  way, and is worth understanding before a sixth attempt.
- Only 4 tick samples (0/50/100/150) were available per run given the
  ~20s duration and the 50-tick print cadence — a longer run (or a finer
  print cadence) would give more windows to assess variability against,
  which might change this verdict in either direction.
- Whether a longer run, or multiple repeated same-toggle runs to
  characterize the baseline variability more thoroughly before attempting
  the A/B, would make the comparison tractable — not attempted here.

## Board

- Row move I would propose for **#71**: stays `HW`, **NOT SUPPORTED**
  rather than `BLOCKED`/`CONFOUNDED` this time — the scene-staticness
  precondition that sank attempts 3/4 appears to have held (extents agree
  closely), but the underlying per-tick metric itself is too noisy
  run-internally for a 3-sample comparison to distinguish a toggle effect
  from ordinary variability. The toggle-binding mechanics remain
  confirmed working (attempts 3, 4, and this run all show the calls
  succeed). A future attempt likely needs either a much longer capture
  window (more ticks to average over) or a way to characterize/reduce the
  intra-run variability before a short A/B can be interpreted.
