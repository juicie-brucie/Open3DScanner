# PLAN #141 — the live tick split (wait/work device-side distribution) — PASS, with a caveat on the UI-open phase

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server (build-tree copy; other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server, mtime 01:11:42, refreshed only on an app build per #107)
- Preflight: pwd OK; no conflicting process; vendor RPCServer daemon running; freshness: binary 2026-10-20 01:11:42 >= newest source main.cpp 01:11:38
- Interposer: `[vulkan_disable_interpose] loaded` expected/confirmed, line 2 of output
- Run start: 2026-10-20 01:17:14
- Device: Creality Raptor Pro, daemon running, device enumerated, no other SDK user

## Command

```
./build/tools/scan_server/scan_server --live 8787
```

**Caveat, stated plainly:** the dispatch asked for "~2 minutes with the UI
open and closed." This agent has **no browser-opening capability in this
run** (no coordinator/browser-pane tool available this session, unlike
the #112 session where the coordinator connected their own pane). The
entire ~2-minute run below was captured with **no UI client ever
connected** — confirmed via `lsof -i :8787` at the 60s and 120s marks
(only the `LISTEN` socket, no `ESTABLISHED` connection both times) and via
every `[perf]` line's own `clients=0.0` field. **No UI-open data was
captured for this run** — flagging this rather than guessing or silently
only covering half the ask.

## Result

- Exit code: `0` (`[launcher] Worker exited normally with code 0.`, clean `SIGINT` teardown)
- Verdict: **PASS** (clean run; UI-open phase not exercised, see caveat above)
- `tracking_status samples=3837 histogram: SUCCESS=3837`; `Frame protocol V1 counters: cloud_frames=1048 full_detail_chunks=4`
- Raw log: `2026-10-20-plan141-live-tick-split.raw.log` (same directory)

## The new instrumentation fields (#139's `wait` segment + #141's `exposure`/`release`)

Confirmed in source before running (`tools/scan_server/perf_window.h`):
the segment enum is now `pose, getter, post, bcast, wait, exposure,
release` (7 segments, up from #111's original 4), plus `overshoot`
(`wait - nominal`) and `residual`. 24 `[perf] window` lines + 1 `final` +
1 `summary` were printed over the ~115s run (nominal 5s cadence).

### Cross-reference to the #112 session's finding

The earlier #112 addendum (`2026-10-19-plan112-conditions-4-6-hud-capture.md`)
found that the old 4-segment breakdown (pose/getter/post/bcast) summed to
only ~1-2ms while `tick p50` ran 110-130ms, and traced the gap in source to
`CFRunLoopRunInMode`'s pacing call sitting inside the tick measurement
window, uninstrumented at the time. **This run's data confirms that
reading and adds a second, previously-invisible contributor:**

### 1. Steady-state (p50) distribution

Across all 24 window lines, `pose`, `getter`, `post`, `bcast` stay
negligible (p50 all 0.00ms except `post`, which runs ~0.2-0.6ms). **`wait`
p50 is consistently ~102-104ms** against a `nominal=100.00ms` target —
i.e. the pacing call accounts for essentially the entire `tick p50`
(114-147ms across the run). **`exposure` p50 is small but non-trivial and
present in every window — ranging ~10-44ms** (e.g. window at line 7556:
`exposure p50=44.59ms`). `release` p50 is 0.00ms in every single window.

**So in steady state: `wait` (the pacing call) is the dominant cost,
exactly as the #112 session inferred; `exposure` is a smaller but
consistently-present secondary cost; `release` is negligible.**

### 2. The worst-case tail — `exposure`, not `wait`, dominates the biggest spikes

This is the new finding #141's instrumentation reveals that #112 could
not see. The three largest `tick max` values in this run:

| Window (line) | tick max | wait max | exposure max | overshoot max |
| --- | --- | --- | --- | --- |
| 7556 | **701.34 ms** | 104.64 ms (nominal) | **597.64 ms** | 4.64 ms (nominal) |
| 7195 | **507.39 ms** | 144.51 ms | **404.19 ms** | 44.51 ms |
| 4588 | **558.24 ms** | 107.76 ms | **454.78 ms** | 7.76 ms |

**In the single largest spike in this run (701.34ms), `wait` was
essentially at its nominal value (104.64ms, overshoot only 4.64ms) — the
entire spike is explained by `exposure` alone (597.64ms), which together
with a normal `wait` accounts for nearly the full `tick max`.** The same
pattern holds for the other two large spikes: `exposure`'s contribution is
2.8-4x larger than `wait`'s overshoot in each case. A few windows (line
2411: `wait max=201.99ms`, `overshoot max=101.99ms`; line 10279: `wait
max=166.36ms`) show `wait` itself overshooting substantially with
`exposure` comparatively modest that window — so both mechanisms produce
large spikes independently, not just together.

**`release` never contributes measurably to any spike** — max values stay
at 0.00-0.02ms in every window across the entire run.

### 3. Summary line (full run)

```
[perf] summary ticks=1047 mean_points=10051.5 mean_bytes=120850.8
pose_mean=0.00ms pose_max=0.06ms
getter_mean=0.00ms getter_max=0.07ms
post_mean=0.45ms post_max=1.40ms
bcast_mean=0.00ms bcast_max=0.05ms
wait_mean=102.99ms wait_max=201.99ms
exposure_mean=27.17ms exposure_max=597.64ms
release_mean=0.00ms release_max=0.02ms
overshoot_mean=2.99ms overshoot_max=101.99ms (wait - nominal; positive = the pacing call ran long)
residual_mean=0.01ms
```

`exposure_max` (597.64ms) exceeds `wait_max` (201.99ms) by nearly 3x
across the whole run — the single largest untimed-call cost by a wide
margin is the exposure re-assert, not the pacing wait, at the extreme end,
even though `wait` dominates the typical/median case.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-20-plan141-live-tick-split '2026-10-20 01:17:14' scan_server`:

> hw-postflight: no crash report for 'scan_server' at/after 2026-10-20 01:17:14 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.

## Open questions

- **UI-open phase not captured at all** (see caveat above) — a rerun with
  an actual browser client connected is needed to see whether `clients>0`
  changes this distribution (e.g. whether `bcast` or `exposure` behave
  differently with a real client pulling frames).
- What specifically makes `exposure` (the vendor exposure re-assert call)
  take up to 597ms on some ticks and ~10ms on others — not investigated;
  this agent only observed and tabulated the numbers from the log.
- Whether `wait`'s own occasional large overshoots (201.99ms, 166.36ms)
  and `exposure`'s large spikes are correlated (same ticks) or independent
  — the table above shows at least one case of each happening without the
  other being unusual, but a full tick-by-tick correlation wasn't done.

## Board

- Row move I would propose for **#141**: stays `HW`, not fully `DONE` —
  the device-side wait/work split is captured and answers most of the
  question (wait dominates typical-case, exposure dominates worst-case
  tail, release is negligible), but the UI-open half of the ask was not
  exercised in this run. A short follow-up with a browser client connected
  would complete it.
