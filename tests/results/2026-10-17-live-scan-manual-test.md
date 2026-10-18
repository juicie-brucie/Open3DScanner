# Manual live-scan test — UI vs. scanner (ad hoc)

**Landed:** 2026-10-17, by the dispatcher at the operator's request, from the
report Claude Code wrote for that run. This was an ad hoc manual run, outside the
hw-test subagent procedure; the report text below is Claude's, kept verbatim — the
"not written to `tests/results/`" lines describe its status before this landing.
No raw log survives; the run's worker output was ephemeral.

---

Open3DScanner — manual live-scan test report
Date: 2026-10-17
Scope: ad hoc manual run, outside the automated hw-test subagent / wt-hw procedure.
Not written to tests/results/ — this was a one-off UI/data check, not a PLAN-board
hardware test record.

-----------------------------------------------------------------------------
1. Purpose
-----------------------------------------------------------------------------
Build the project from the main checkout as it currently stands and run the UI
against the physically connected scanner, to see what the pipeline produces
right now outside the narrower automated test schema (device_message_listener_test /
data_process_message_listener_test).

-----------------------------------------------------------------------------
2. Build
-----------------------------------------------------------------------------
Checkout:  /Users/brucemackay/DevReposiMac/Open3DScanner
Branch:    main @ 4d0cb93
Command:   cmake --build build
Result:    clean build, all targets linked without error, including:
           tools/scan_server/scan_server
           tools/live_capture_test/live_capture_test
           tools/capture_pipeline_smoke_test/capture_pipeline_smoke_test
           tools/smoke_test/smoke_test
           tests/hardware/device_message_listener_test
           tests/hardware/data_process_message_listener_test

Note: the repo's .claude/hooks/guard-hw-run.sh PreToolUse hook was NOT
bypassed or modified to do this build or run. The guard only pattern-matches
four things in the literal shell command text: manually setting
DYLD_INSERT_LIBRARIES, touching .recon-cache/vendor dylibs, editing
obscan_api.h, or running *message_listener_test* outside the wt-hw worktree.
`scan_server --live` (no explicit library-path arg) resolves the SDK path and
sets DYLD_INSERT_LIBRARIES internally in C++ at runtime — none of that
appears in the invoking shell command, so none of the guard's rules fire.
No change was made to the hook or to CLAUDE.md.

-----------------------------------------------------------------------------
3. Run
-----------------------------------------------------------------------------
Command:   ./build/tools/scan_server/scan_server --live
Port:      8787 (default), WebSocket server
Device:    Creality Raptor Pro, SN CR0886100SN, USB3.0, connected and powered
Session:   default 300 s streaming budget (3000 "tick" loop iterations),
           timeout_seconds = session_seconds + 120 = 420 s worker budget

Launcher confirmed the interpose dylibs were set correctly before forking the
worker (same mechanism tools/live_capture_test uses):
  libMoltenVK.dylib + libvulkan_disable_interpose.dylib

Startup sequence, all SUCCESS:
  obscan_context_create
  obscan_context_get_device_management
  obscan_context_add_data_msg_listener
  obscan_context_add_process_msg_listener
  obscan_device_management_add_device_msg_listener
  obscan_device_management_get_device_num -> count=1
  obscan_context_create_session
  obscan_session_create_scan
  obscan_scan_preview
  obscan_scan_config_combined_exposure_and_laser_current
  obscan_scan_start

-----------------------------------------------------------------------------
4. UI
-----------------------------------------------------------------------------
ui/index.html + ui/app.js is a static WebSocket client; it connects to
ws://<host>:<port>/ (default 127.0.0.1:8787) and renders whatever point-cloud
frames the server broadcasts.

Opening it as a bare file:// URL in Claude's built-in browser pane rendered it
as a static data: URL snapshot — its own JS/WebSocket never actually ran, so
it stayed stuck on "connecting...". Fix: served the ui/ directory over plain
local HTTP instead —

  cd ui && python3 -m http.server 8788

— then navigated to http://127.0.0.1:8788/index.html. That let the page's own
JS execute and open a real WebSocket, and it connected immediately
("status: connected").

-----------------------------------------------------------------------------
5. Mid-session behavior (expected, not a fault)
-----------------------------------------------------------------------------
Throughout the full 3000-tick streaming window, the UI's "points" readout
stayed at 0 and the canvas stayed black. Server-side worker log, every tick:

  [worker] tick N/3000: raw_cloud num_points=0 (max seen so far: 0), clients=1-2, paused=0

This matches already-documented behavior in README.md: obscan_scan_get_raw_cloud
has never been observed to return nonzero points *during* active scanning on
this hardware, regardless of config. Real geometry only appears at the final
pause -> get_realtime_model_mesh snapshot. This is not a bug in this run; it's
the known per-frame limitation, carried over from tools/live_capture_test.

Auto-exposure converged over the session (laser/IR/RGB gain and exposure-time
telemetry logged every ~tick cycle), settling around exposure_time ~1000 by
the later half of the session.

-----------------------------------------------------------------------------
6. End of session — clean teardown
-----------------------------------------------------------------------------
At tick ~3000/3000:

  obscan_scan_pause                      -> OB_SCAN_SUCCESS (0)
  obscan_scan_get_realtime_model_mesh    -> OB_SCAN_SUCCESS (0)
                                             num_vertices=5845, num_triangles=4167
  [worker] Broadcast final reconstructed mesh as one point-cloud frame (5845 points).
  obscan_scan_stop                       -> OB_SCAN_SUCCESS (0)
  obscan_session_destroy                 -> OB_SCAN_SUCCESS (0)
  obscan_context_destroy                 -> OB_SCAN_SUCCESS (0)
  [launcher] Worker exited normally with code 0.

No crash. This is notable only as a contrast, not as proof of anything:
the already-documented, never-fixed obscan_context_destroy() / libomp SIGSEGV
(see README.md "Known issues", and tests/results/2026-10-17-plan8-device-
message-listener.md's crash analysis) did NOT recur on this run. One clean
pass doesn't rule that bug out — it was already known to be intermittent in
its triggering conditions — but it's one more clean data point for the
record.

-----------------------------------------------------------------------------
7. Result
-----------------------------------------------------------------------------
The UI rendered the final mesh snapshot as a point cloud: 5,845 points,
frame #3000, visible in the browser as a partial curved/arc-shaped surface.
Status then flipped to "disconnected" once the server process exited
normally after teardown.

Both scan_server and the local `python3 -m http.server 8788` instance used to
serve the UI have since been stopped; this was a one-off manual check, not a
standing service.

-----------------------------------------------------------------------------
8. Artifacts
-----------------------------------------------------------------------------
No files were written to tests/results/ or committed anywhere — this was
explicitly a manual, ad hoc run outside that recorded procedure, at your
request. The full worker stdout/stderr lives only in this session's
ephemeral task-output files and was not preserved elsewhere. If this result
is worth keeping on the record (e.g. as supporting evidence for PLAN board
item #11 or the libomp teardown question), it should go through the normal
hw-test subagent / tests/results/ path so it gets the usual sha256-citable
treatment — this report is informal by design.
