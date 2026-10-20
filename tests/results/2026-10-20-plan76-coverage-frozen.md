# Row #76 — the coverage heatmap observation, retried with a browser pane — PRESENT BUT NON-RESPONSIVE (not acceptance)

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: `fc6a081` (Merge pull request #407 from juicie-brucie/dispatch/cleanup-log)
- Target: build/tools/scan_server/scan_server (build-tree copy)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (mtime 17:26:47, refreshed only on an app build per #107)
- Preflight: pwd OK; no conflicting process; vendor RPCServer daemon running; freshness: binary 2026-10-20 17:26:47 >= newest source main.cpp 17:26:42
- Interposer: `[vulkan_disable_interpose] loaded` expected/confirmed, line 2 of output
- Run start: 2026-10-20 17:29:57
- Device: Creality Raptor Pro, serial `CR0886100SN`, USB3.0, daemon running, device enumeration directly confirmed (`count=1`) before committing to the run
- One-SDK-user confirmed: `pgrep -x scan_server`/`raptor_client`/etc. empty before this run
- Scene/physical setup: **this row needed scanner movement, not the static setup** — identified before starting by checking the `coverageVisited`/`coverageSeen` semantics in `ui/app.js` (visited tracks scanner-pose proximity, seen tracks captured surface; a static scanner wouldn't meaningfully exercise the heatmap). Confirmed with the user before this run; the user actively moved the scanner around the object during the observation.

## This is a follow-up to the earlier blocked attempt

`2026-10-20-plan76-coverage-heatmap-blocked.md` found the device/daemon/
scene side of this row's precondition fully satisfied but could not
observe the actual coverage/overlay data — this agent has no
browser-pane tool. This run used the coordinator's browser pane
(same approach as the #112 session) to sample the data directly.

## Command

```
./build/tools/scan_server/scan_server --live 8787
```

Run ended with a clean `SIGINT` teardown (`[worker] obscan_scan_pause` →
`obscan_scan_get_realtime_model_mesh` → `Worker exited normally with code
0`). `tracking_status samples=2921, histogram: SUCCESS=2921` (100%).
`Frame protocol V1 counters: cloud_frames=1011 full_detail_chunks=141`.

## The coordinator's sampling (their own observation, relayed verbatim)

The coordinator connected a browser pane to `http://127.0.0.1:8787/` and
sampled `window.openScannerCoverage()` — the debug hook in `ui/app.js`
(`{visited, seen, underCoveredPoints}`) — six times over ~25 seconds while
the user actively moved the scanner around the object (confirmed by the
user in chat: "moving scanner"):

| t | seen | underCoveredPoints | visited |
| --- | --- | --- | --- |
| 0s (pre-movement) | 176 | 6667 | 125 |
| 2s | 564 | 6667 | 125 |
| 7s | 1797 | 6667 | 125 |
| 12s (movement confirmed in progress) | 2680 | 6667 | 125 |
| 17s | 3153 | 6667 | 125 |
| 25s | 3513 | 6667 | 125 |

**`visited` and `underCoveredPoints` never changed across the entire
window — not once, including 15+ seconds of confirmed active scanner
movement.** `seen` climbed normally throughout (176 → 3513), and the
point count grew (18,412 → 19,539+ per the coordinator), confirming data
was genuinely flowing and the scanner was capturing new content the whole
time.

The HUD's own `coverage` line (page text, not just the console hook)
matched the console numbers exactly: **"coverage 0% covered, 6667
under-covered pts" was identical on every read, the whole session.**

Visually: two screenshots taken 8 seconds apart, with movement happening
between them, show the red overlay in essentially the same shape/region
both times — consistent with the numeric freeze, not a stale-screenshot
artifact.

## Result

- Exit code: `0`
- Verdict: **PRESENT BUT NON-RESPONSIVE — not acceptance.** The row's
  owed observable (`coverage` + `tracking` HUD lines, and the red overlay
  visible on the live cloud) is **present and renders** — all three
  elements exist and display values/color on screen, which is a genuine
  improvement over the earlier `BLOCKED` attempt. But the row's actual
  intent — a coverage heatmap that responds to new scan data as the
  operator moves the scanner — **was not observed**: the under-covered
  set and the visited-voxel count stayed frozen at their very first
  computed values for the entire ~25s sampling window, including a
  confirmed period of active scanner movement, while the underlying data
  (`seen`, point count) demonstrably kept updating normally in parallel.
- Raw log: `2026-10-20-plan76-coverage-frozen.raw.log` (same directory)

## Crash sweep

`scripts/hw-postflight.sh 2026-10-20-plan76-coverage-frozen '2026-10-20 17:29:57' scan_server`:

> hw-postflight: no crash report for 'scan_server' at/after 2026-10-20 17:29:57 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.

## On failure or block — observable + one lead

- Observable: `window.openScannerCoverage()` sampled six times over ~25s
  during confirmed active scanner movement returned `visited=125` and
  `underCoveredPoints=6667` identically every time, while `seen` climbed
  176→3513 and point count climbed over the same window — a clean,
  repeatable split between a value that updates (`seen`) and two that
  don't (`visited`, `underCoveredPoints`) from the same computation.
- Lead: the `coverageVisited`/`coverageSeen` update logic in `ui/app.js`
  — specifically whatever triggers a `visited`-set update (the function
  `markVisited()` and its call site) appears not to be firing, or is
  gated on something that didn't fire during this run. Not diagnosed
  further here, per role — flagged for a human/builder to check the
  update-trigger logic against this observable.

## Open questions

- Is the visited/under-covered computation supposed to update
  continuously as frames arrive, or only periodically/on some other
  trigger (e.g. a specific pose-change threshold) that this ~25s window
  didn't reach? Not determined here — the row's own text ("the vendor's
  per-frame tracking status beside it") and the HUD's rAF-driven refresh
  suggest continuous updates were intended, but this agent is not
  confirming that from source.
- Would a longer observation window (minutes, not ~25s) eventually show
  `visited`/`underCoveredPoints` change, or is the freeze permanent for
  the session? Not tested here.
- Is `markVisited()` actually being called at all during a live session
  (as opposed to only in the synthetic/demo path), or is it wired to a
  pose source that isn't populated in live mode? Not checked — a
  source-level question for whoever picks this up next.

## Board

- Row move I would propose for **#76**: stays `HW`, **not** `DONE`. The
  UI elements this row asked to observe are confirmed present and
  rendering (closing the earlier `BLOCKED` tooling gap), but the
  underlying coverage computation is demonstrably not responding to new
  scan data during active movement — a real functional gap, not a
  tooling or precondition issue this time. Recommend filing this as its
  own follow-up row for the `coverageVisited`/`markVisited()` update-
  trigger logic in `ui/app.js`, separate from re-confirming #76's own
  acceptance once that's fixed.

## Addendum — end-of-session screenshot, a further confirming data point

The user provided a screenshot taken at the very end of this run, after
disconnect/teardown: `2026-10-20-plan76-coverage-frozen-final-screenshot.png`
(same directory as this record). At that point the HUD shows:

- `points`: **2,808,248** (the final accumulated total — this matches the
  server's own teardown line, `obscan_scan_get_realtime_model_mesh ->
  OB_SCAN_SUCCESS (0), num_vertices=2808248`, far beyond the 19,539+ this
  record's mid-run sampling reached)
- `status`: `disconnected`
- `frame`: `#-1` (stale, post-disconnect)
- `coverage`: **still "0% covered, 6667 under-covered pts"** — the exact
  same frozen value from every earlier sample in this record

This is a further, independent confirming data point for the freeze
already documented above: even with the point count having grown roughly
**140x** beyond the last live sample (19,539 → 2,808,248) by the time the
session ended, the coverage computation's output never changed from its
initial value at any point, all the way through to disconnect. Does not
change the verdict or the lead already recorded above — included here as
additional evidence for whoever investigates the `markVisited()`/
`coverageVisited` update-trigger logic.
