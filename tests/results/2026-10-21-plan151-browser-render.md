# PLAN #151 — browser render over ws://8787, real scan — PASS (browser half)

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server  (build-tree copy, fixed order 2)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (12:53:07) — not used
- Preflight: pwd OK; no conflicting process; daemon: RPCServer(12187) running; freshness: binary
  12:53:07 >= newest source main.cpp 12:52:55
- Interposer: expect line 2 `[vulkan_disable_interpose] loaded` (scan_server sets
  DYLD_INSERT_LIBRARIES itself) — confirmed present
- Run start: 2026-10-21 22:53:46

## Command

```
./build/tools/scan_server/scan_server --live 8787
```
Operator then opened `http://127.0.0.1:8787/` in a browser on the same Mac.

## Result

- Exit code: `0` (`[launcher] Worker exited normally with code 0.`) — the session reached its
  bounded length (3000 ticks / "up to 300s") and ran its own pause/stop/destroy teardown
  automatically; not stopped by the agent
- Verdict: **PASS** (browser render half of #151)
- Raw log: `plan151-browser-render.raw.log` (tee of the live server stdout, same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` (line 2 of worker output)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `scan_server --live` starts, device enumerates, scan starts | **[PASS]** `obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1`; `obscan_scan_start -> OB_SCAN_SUCCESS (0)` |
| 2 | WS server listens on :8787 | **[PASS]** `[worker] Streaming live scan frames on ws://0.0.0.0:8787`; independently confirmed via `lsof -nP -iTCP:8787 -sTCP:LISTEN` -> `scan_serv 12403 ... LISTEN` |
| 3 | Browser connects and renders the point cloud | **[PASS]** operator-attributed screenshot, viewed directly: `/private/tmp/claude-501/-Users-brucemackay-DevReposiMac-Open3DScanner/5d12e442-6c1c-42cc-8101-c4cff4dd42ec/images/1.png` — sparse red/white line-cluster point cloud rendering against black background |
| 4 | HUD reports live values, attributed read | **[PASS]** HUD text, read directly from the screenshot: `backend webgpu`, `status connected`, `scan scanning (live)`, `frame age p50 6.8 / p95 15.0 ms`, `data 10.0 fps client`, `render 59.9 fps client`, `points 4667`, `coverage 19% covered, 3548 under-covered pts`, `tracking SUCCESS (0)`, `frame #1655` |
| 5 | HUD tracking value is consistent with server log at the time | **[PASS]** raw log at/near the screenshot window shows `FRAME_READY tracking_status=0 (SUCCESS)` repeating, matching the HUD's `tracking SUCCESS (0)` |

## Crash sweep

`scripts/hw-postflight.sh plan151-browser-render '2026-10-21 22:53:46' scan_server` ->
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-21 22:53:46 — safe to record
'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

Session-end teardown was clean: `obscan_scan_stop -> OB_SCAN_SUCCESS (0)`,
`obscan_session_destroy -> OB_SCAN_SUCCESS (0)` (5 polls, 4 refusals, 0.5s),
`obscan_context_destroy -> OB_SCAN_SUCCESS (0)` (1 poll, 0 refusals, 0.4s). Final
`tracking_status` histogram for the whole session: `SUCCESS=7874 LOST=63 FAILURE_MARKERLESS=1109`
(`cloud_frames=3001`).

## Open questions

- The HUD reads `backend webgpu`; the README describes the renderer as WebGL2, not WebGPU — is
  that label stale, or did the renderer path change since the README was last updated? Not
  investigated further here (observation only).
- Coverage reads 19% with a sparse, cross-hatched point pattern rather than a dense surface —
  is that expected for this scene/distance, or does it reflect scan geometry not yet settled at
  frame #1655? Not something this record can answer from a single frame.
