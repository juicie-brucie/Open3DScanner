# PLAN #25 manual rerun — operator, view-only (ad hoc)

**Landed:** 2026-10-17, by the dispatcher from the planner's handoff, at the operator's
request; the report below is the operator's, kept verbatim — its "lives outside the repo
and was not committed" lines describe its status before this landing. The run was
explicitly read-only: it wrote no other files and made no board edits.

---

Open3DScanner — PLAN #25 manual rerun (view-only)
Date: 2026-10-17
Scope: ad hoc manual rerun, explicitly read-only per request — no files written to
tests/results/, no PLAN.md edits, no commits. This report itself lives outside the
repo and was not committed anywhere.

-----------------------------------------------------------------------------
1. What this was
-----------------------------------------------------------------------------
PLAN board item #25 (docs/recomp/PLAN.md): "Poll
obscan_scan_get_model_point_color_clouds per tick in scan_server so the live view
publishes." The fix landed earlier via PR #67 (a5a78e7) — scan_server now calls the
color-cloud getter every tick instead of only polling obscan_scan_get_raw_cloud
(which this project has never observed returning nonzero points during active
scanning). At the time of this rerun, PLAN #25 was still at status HW: the code had
landed, but the actual hardware acceptance run (OQ-S5 — does the live view really
show points while scanning, not just at the end) had not yet been watched.

This rerun was purely to see it for myself before any official record existed.

-----------------------------------------------------------------------------
2. Build
-----------------------------------------------------------------------------
Checkout:  /Users/brucemackay/DevReposiMac/Open3DScanner (main)
Rebuilt scan_server fresh immediately before running, specifically because the
binary already sitting in build/ was stale — it had been compiled from main's tip
*before* PR #67 merged, so it would have silently repeated the old points=0
behavior instead of testing the fix. Confirmed via source-vs-binary timestamp
mismatch (tools/scan_server/main.cpp's last commit postdated the existing binary).

  cmake --build build --target scan_server

-----------------------------------------------------------------------------
3. Run
-----------------------------------------------------------------------------
Command:  ./build/tools/scan_server/scan_server --live
Device:   Creality Raptor Pro, SN CR0886100SN, USB3.0, connected and powered
UI:       ui/index.html served locally via `python3 -m http.server 8788` (a bare
          file:// open renders as a static snapshot in Claude's browser pane and
          never actually opens the WebSocket — same caveat as the earlier manual
          test tonight).

No hook bypass was needed or attempted — `scan_server --live` doesn't match any of
guard-hw-run.sh's four blocked patterns (no literal DYLD_INSERT_LIBRARIES= in the
invoking command, no vendor-artifact path string, no obscan_api.h reference, no
message_listener_test substring).

-----------------------------------------------------------------------------
4. Observation
-----------------------------------------------------------------------------
This is the material difference from the pre-PR-#67 manual test run earlier
tonight (which stayed at points=0 for all 3000 ticks of streaming and only showed
geometry at the final post-pause mesh snapshot):

  Early in the session (~frame 60):   status=connected, scan=scanning (live),
                                       points=293
  Later in the session (~frame 113):  points=11,958, visible structure forming
                                       in the viewport (a recognizable
                                       frame/stand shape with cross-hatched
                                       laser-line geometry)

Live points accumulated continuously *during* active scanning this time, which is
exactly OQ-S5's acceptance condition. Stopped by request partway through the
session (not run to completion) — this was a look, not a full timed run.

-----------------------------------------------------------------------------
5. Shutdown
-----------------------------------------------------------------------------
Stopping scan_server took two steps: `pkill -f "scan_server --live"` only killed
the launcher process; the actual worker child (`scan_server --live-worker ...`,
which holds the port and the device) survived that and had to be killed directly
by PID. Confirmed afterward: no scan_server processes, no UI HTTP server, ports
8787/8788 both free.

-----------------------------------------------------------------------------
6. Cross-reference — an official run landed independently, same evening
-----------------------------------------------------------------------------
Separately from this manual rerun, the recomp/opencode automation landed its own
official acceptance record for the same PLAN #25 item, at main commit dad1886
(PR #72, "hw: OQ-S5 acceptance run — live points during scanning (#25 DONE, #30
filed)"), written to tests/results/2026-10-17-scan-server-live-acceptance.md.
That run reports color_cloud num_points going nonzero (292 -> 883, growing) from
tick 50/300 onward at every sampled tick with paused=0, exit 0, clean teardown —
PLAN #25 is now marked DONE on the board. It also filed a new open item, #30: in
that run, raw_cloud num_points tracked color_cloud num_points almost exactly at
every sampled tick, which contradicts PLAN #23's earlier characterization of
raw_cloud staying at 0 during active scanning. Worth a look if raw_cloud's
behavior matters to anything downstream — not something this manual rerun
investigated.

This report describes only what I personally watched in this rerun; it is not a
substitute for, and does not modify, that official PLAN #72 record.
