# live_capture_test — session/scan lifecycle against real hardware — PASS

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/live_capture_test/live_capture_test (build-tree copy (fixed order: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; freshness: binary 2026-10-18 21:46:44 >= newest source main.cpp 2026-10-17 23:32:03
- Interposer: expect first line: `[launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib`
- Run start: 2026-10-19 16:08:22
- Device: Creality Raptor Pro (CR0886100SN), USB3.0, connected (detected mid-run)

## Command

```
build/tools/live_capture_test/live_capture_test
```

(No scene/calibration target placed in front of the scanner for this run —
no instruction from the test's own usage/env-var comments called for one,
and nothing it printed asked for one. See open question below.)

## Result

- Exit code: `0`
- Verdict: **PASS** — full lifecycle (`obscan_context_create_session` ->
  `obscan_session_create_scan` -> `obscan_scan_preview` -> `obscan_scan_start`
  -> 60x `obscan_scan_get_raw_cloud` poll -> `obscan_scan_stop` ->
  `obscan_session_destroy` -> `obscan_context_destroy`) completed with every
  SDK call returning `OB_SCAN_SUCCESS`, worker exited 0, launcher reported
  `Worker exited normally with code 0.`
- Raw log: `2026-10-19-live-capture-test.raw.log` (same directory)
- Interposer confirmation: `[launcher] DYLD_INSERT_LIBRARIES=/Applications/CrealityScan.app/Contents/PlugIns/libMoltenVK.dylib:/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw/build/engine/libvulkan_disable_interpose.dylib` (line 1); `[vulkan_disable_interpose] loaded` (line 3, after `[launcher] Running live capture session in a worker process...`).

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `obscan_context_create` / `get_device_management` | **[PASS]** both `OB_SCAN_SUCCESS` |
| 2 | data/process/device message listeners add | **[PASS]** all three `OB_SCAN_SUCCESS` |
| 3 | device enumerated after connect | **[PASS]** `get_device_num -> OB_SCAN_SUCCESS, count=1` |
| 4 | session/scan create | **[PASS]** `obscan_context_create_session`, `obscan_session_create_scan` both `OB_SCAN_SUCCESS` |
| 5 | `obscan_scan_preview` to completion | **[PASS]** progress callbacks ran 0.05 -> 1.000, `preview status callback: OB_SCAN_SUCCESS (0)` |
| 6 | `obscan_scan_start` | **[PASS]** `OB_SCAN_SUCCESS` after `frame_ready_count=10` |
| 7 | 60x `obscan_scan_get_raw_cloud` poll, no crash | **[PASS]** all 60 polls returned `OB_SCAN_SUCCESS`; **all 60 reported `num_points=0`** (see open question) |
| 8 | `obscan_scan_stop` / `obscan_session_destroy` / `obscan_context_destroy` | **[PASS]** all three `OB_SCAN_SUCCESS`, no crash signal, no `worker did not exit cleanly` line |

## Crash sweep

`scripts/hw-postflight.sh 2026-10-19-live-capture-test '2026-10-19 16:08:22' live_capture_test`:

> no crash report for 'live_capture_test' at/after 2026-10-19 16:08:22 — safe to record 'no crash report (checked DiagnosticReports for live_capture_test at/after the run start)'.

## Open questions

- All 60 `obscan_scan_get_raw_cloud` polls returned `num_points=0` for the
  whole ~60s capture window, alongside repeated `Circle Points Unchange` /
  `Circle Points Slightly Dark` auto-exposure log lines and several "frame
  will be dropped because the data size does not match expectation"
  warnings. Is zero points across every poll expected when nothing is
  positioned for the depth sensor to resolve (no scan target was placed for
  this run), or does it indicate a capture-path problem independent of scene
  content? Not settled here — this run only reports the observable.
- Several `VideoSensor.cpp:455` "frame will be dropped" and `Pipeline.cpp:310`
  "Wait for frame timeout" warnings appeared throughout — are these expected
  background noise at this exposure/frame-rate combination, or a sign of a
  marginal capture path?

## Board

- Row move: none proposed — I found no PLAN.md row specifically tracking
  `live_capture_test` runs (rows #6/#12 cover an earlier doc-accuracy issue
  in its comments, already `DONE`). Dispatcher may want to file a new row
  for the all-zero-points observation above if it's unexpected.
