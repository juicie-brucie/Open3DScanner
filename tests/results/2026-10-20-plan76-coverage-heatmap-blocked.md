# Row #76 — the coverage heatmap observation — BLOCKED (tooling gap, not a precondition problem)

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: `71aadd5`
- Target: build/tools/scan_server/scan_server (build-tree copy)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (mtime 03:54:32, refreshed only on an app build per #107)
- Preflight: pwd OK; no conflicting process; vendor RPCServer daemon running; freshness: binary 2026-10-20 03:54:32 >= newest source control_json.h 03:54:28
- Interposer: `[vulkan_disable_interpose] loaded` expected/confirmed, line 2 of output
- Run start: 2026-10-20 17:04:34
- Device: Creality Raptor Pro, serial `CR0886100SN`, USB3.0, daemon running, **device enumeration directly confirmed before committing to the run** (`count=1`, not assumed from a prior check)
- One-SDK-user confirmed: `pgrep -x scan_server`/`raptor_client`/etc. empty before this run
- Static scene: same setup as #71 attempt 5 / #153's run (scanner at rest, CrealityScan open, scene positioned) — confirmed appropriate for this row per PLAN.md's own acceptance text before starting

## Command

```
./build/tools/scan_server/scan_server --live 8787
```

## What #76 actually needs — found before completing, not assumed

Row #76's owed observable is "the HUD's `coverage` + `tracking` lines, and
the red overlay visible on the live cloud." Before relying on a server-side
capture, this agent checked where that data actually comes from:

- **`tracking`** is server-side — `main.cpp` builds it into the status
  JSON via `openscanner::tracking::TrackingStatusJsonTail`/
  `TrackingCountsJsonTail` (confirmed in source). This part is capturable
  without a browser.
- **`coverage` and the red under-covered overlay are not.** Checked
  `ui/app.js`: the entire coverage computation — the 5³-voxel
  `coverageVisited`/`coverageSeen` sets, the under-covered budget, the
  percentage text, and the red-painted overlay itself — is **client-side
  JavaScript**, computed from the raw point-cloud frames as they arrive in
  the browser. There is no server-side equivalent field, no status-JSON
  coverage number, nothing this agent can read via a WS probe or the raw
  log. The comment at `ui/app.js:139` ("Debug/acceptance hook: a hardware
  run can read the coverage counters...") confirms this was expected to be
  read out of a running browser instance, not the server.

**This agent has no browser-opening/browser-pane capability in this
session** (same limitation already flagged for #141's UI-open phase and
#153's "picture unchanged" check). Given that, #76's actual owed
observable — the coverage percentage and the visible red overlay — **cannot
be captured at all from this session as currently equipped**, independent
of the device/scene setup, which was confirmed correct.

## What was confirmed

- Preflight OK, device enumeration directly verified (`count=1`) before
  the run, no conflicting process.
- The live run itself was clean: `tracking_status samples=1034
  histogram: SUCCESS=1034` (100%), `Frame protocol V1 counters:
  cloud_frames=336 full_detail_chunks=1`, clean `SIGINT` teardown,
  `Worker exited normally with code 0`, no crash.
- So the device/scene/daemon side of this row's precondition is fully
  satisfied — the blocker is entirely about this agent's tooling, not
  the hardware or the setup.

## Result

- Exit code: `0`
- Verdict: **BLOCKED** — not on hardware, not on preconditions, but on
  this agent's lack of a browser-pane tool to read the client-side
  coverage computation and visually confirm the red overlay.
- Raw log: `2026-10-20-plan76-coverage-heatmap-blocked.raw.log` (same directory)

## Crash sweep

`scripts/hw-postflight.sh 2026-10-20-plan76-coverage-heatmap-blocked '2026-10-20 17:04:34' scan_server`:

> hw-postflight: no crash report for 'scan_server' at/after 2026-10-20 17:04:34 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.

## On failure or block — observable + one lead

- Observable: the run itself succeeded cleanly end-to-end; the block is
  that this agent cannot read `ui/app.js`'s client-side `coverageSeen`/
  `coverageVisited` state or visually confirm the red overlay without an
  actual browser client connected and viewed.
- Lead: not a code lead — this needs either (a) a coordinator/user with a
  browser pane (as was used earlier this session for #112's HUD
  cross-checks), or (b) some server-exposed debug readout of the coverage
  counters if one gets added later, to complete this row's observable.

## Open questions

- Is there any existing way to read `ui/app.js`'s coverage counters
  without a full interactive browser (e.g. a headless/automated capture)
  that this agent isn't aware of? Not found in this session's tooling.

## Board

- Row move I would propose for **#76**: stays `HW` — not confirmed, but
  specifically because of a tooling gap on this agent's side, not because
  the feature doesn't work or the hardware setup was wrong. Worth noting
  for the dispatcher: this row's observable may need a session with
  browser-pane access (like the #112 session had) rather than a plain
  hw-test agent run.
