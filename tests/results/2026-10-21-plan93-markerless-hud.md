# PLAN #93 — markerless-stretch HUD observation (lens occlusion, browser-rendered) — PASS

Same session as `2026-10-21-plan151-browser-render.md` (same `scan_server --live 8787` process,
same browser tab).

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server (same instance as the #151 record)
- Preflight / interposer: as recorded in `2026-10-21-plan151-browser-render.md` (same run)
- Run start: 2026-10-21 22:53:46 (same session)

## Command

No new command — this is a physical-occlusion step against the already-running session from
#151 (`./build/tools/scan_server/scan_server --live 8787`, browser open at
`http://127.0.0.1:8787/`). Operator covered the scanner's lens by hand for approximately 10 s
(operator's verbal estimate), then uncovered it.

## Result

- Exit code: `0` (`[launcher] Worker exited normally with code 0.`) — the session reached its
  bounded length and tore down on its own after this occlusion step; not stopped by the agent
- Verdict: **PASS**
- Raw log: `plan93-markerless-hud.raw.log` (snapshot of the live server stdout, same directory)
- Interposer confirmation: as in the #151 record (same process)

## Operator-attributed screenshots, viewed directly

- Mid-occlusion: `/private/tmp/claude-501/-Users-brucemackay-DevReposiMac-Open3DScanner/5d12e442-6c1c-42cc-8101-c4cff4dd42ec/images/2.png`
  — HUD: `tracking FAILURE_MARKERLESS (5)`, frame `#3571`, `coverage 31% covered`
- Post-uncover: `/private/tmp/claude-501/-Users-brucemackay-DevReposiMac-Open3DScanner/5d12e442-6c1c-42cc-8101-c4cff4dd42ec/images/3.png`
  — HUD: `tracking SUCCESS (0)`, frame `#4199`

Both screenshots show the `tracking` line rendering the vendor's raw value directly — no
`markerless window k/3` or similar client-side annotation next to it in either state.

## Server log cross-check (raw `FRAME_READY tracking_status=` values, collapsed by run)

```
4996 x SUCCESS (0)
  32 x LOST (1)
 108 x SUCCESS (0)      <- brief recovery blip before settling
  31 x LOST (1)
1109 x FAILURE_MARKERLESS (5)
1062 x SUCCESS (0)      <- post-uncover recovery, direct, no extra state
```

The two short `LOST` runs before the sustained `FAILURE_MARKERLESS` stretch match the pinned
hysteresis described in row #81 (`tracking-mechanism.md` §1): the vendor reports the first two
occurrences as `LOST`, the third onward as `FAILURE_MARKERLESS`. Interpolating against the
worker's periodic `tick N/3000` lines (~103 ms/tick, run-loop floor), the sustained
`FAILURE_MARKERLESS` span is approximately **tick ~1719 to ~2081** (~362 ticks, ~36–37 s) — this
measures only the settled `FAILURE_MARKERLESS` portion of the log, not the full covered-to-
uncovered interval, and it is **longer** than the operator's ~10 s verbal estimate (open
question below).

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | A markerless stretch is produced by physical lens occlusion | **[PASS]** `FAILURE_MARKERLESS (5)` sustained for 1109 consecutive `FRAME_READY` events in the raw log |
| 2 | HUD renders the raw vendor value with no `markerless window k/3` suffix | **[PASS]** screenshot `2.png`: `tracking FAILURE_MARKERLESS (5)`, no counter/annotation text |
| 3 | HUD recovers directly to `SUCCESS` on uncover, no extra recovery-counter text | **[PASS]** screenshot `3.png`: `tracking SUCCESS (0)`, no annotation; log confirms 1062 consecutive `SUCCESS (0)` immediately follow the last `FAILURE_MARKERLESS (5)` |
| 4 | The raw sequence shows the vendor's debounce (LOST x2 before FAILURE_MARKERLESS) | **[PASS]** log: `LOST x32, SUCCESS x108, LOST x31, FAILURE_MARKERLESS x1109` |

## Crash sweep

`scripts/hw-postflight.sh plan93-markerless-hud '2026-10-21 22:53:46' scan_server` ->
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-21 22:53:46 — safe to record
'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

Session-end teardown was clean (see the #151 record for the exact `obscan_*_destroy` sequence).
Final whole-session `tracking_status` histogram: `SUCCESS=7874 LOST=63 FAILURE_MARKERLESS=1109`
— the `FAILURE_MARKERLESS` count (1109) matches this record's mid-session snapshot exactly, so no
further markerless state occurred between the uncover and session end.

## Open questions

- The log-derived `FAILURE_MARKERLESS` span (~36–37 s, tick-interpolated) is longer than the
  operator's ~10 s verbal estimate of how long the lens was covered — is that because the
  operator's estimate was approximate, because repositioning/hand movement kept triggering
  tracking loss beyond the literal "covered" window, or something else? Not resolved here; the
  operator was not asked for a precise stopwatch duration.
- The `LOST x32, SUCCESS x108, LOST x31` pattern before the sustained `FAILURE_MARKERLESS`
  suggests an intermediate brief recovery — was the operator's hand/cloth not fully covering
  the lens for a moment during that window? Not investigated (observation only).
