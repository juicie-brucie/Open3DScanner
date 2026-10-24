# UI End-to-End Baseline — Vendor SDK Path — PASS

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (12:53:07) — refreshed only on an app build (#107)
- Preflight: pwd OK; no conflicting process; daemon: RPCServer(967,) running (process proxy: a vendor RPC host process exists); freshness: binary 12:53:07 >= newest source main.cpp 12:52:55
- Interposer: expect line 2: [vulkan_disable_interpose] loaded  (scan_server sets DYLD_INSERT_LIBRARIES itself) — CONFIRMED present
- Run start: 2026-10-24 03:39:46
- HEAD SHA: de949479a8794c0508d8a5778134cbc2181b9bf9

## Command

```
build/tools/scan_server/scan_server --live 8787
```

(backgrounded with stdout+stderr redirected to log; PID 4241; stopped by SIGTERM after coordinator visual confirmation)

## Result

- Exit code: SIGTERM (stopped cleanly after coordinator confirmation)
- Verdict: **PASS**
- Raw log: `plan-uvc-ui-baseline.raw.log` (same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` — present as line 2

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | RPCServer running before launch | **[PASS]** pgrep -x RPCServer returned PID 967 |
| 2 | Preflight exits 0 | **[PASS]** hw-preflight.sh scan_server: OK |
| 3 | Interposer loaded (line 2) | **[PASS]** `[vulkan_disable_interpose] loaded` present |
| 4 | SDK loads without error | **[PASS]** `obscan_context_create -> OB_SCAN_SUCCESS (0)` |
| 5 | Device found (count=1) | **[PASS]** `obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1` |
| 6 | Scan started | **[PASS]** `obscan_scan_preview -> OB_SCAN_SUCCESS (0)` |
| 7 | FRAME_READY lines flowing | **[PASS]** multiple `FRAME_READY tracking_status=0 (SUCCESS)` lines observed at 15 s mark |
| 8 | UI HTTP serving on port 8787 | **[PASS]** `curl http://127.0.0.1:8787/` returned `<!doctype html>` OpenScanner page |
| 9 | Browser visual — live data | **[PASS]** frame #1067, tracking SUCCESS (0), 19894 points, 60.1 fps render |
| 10 | Browser visual — density colouring | **[PASS]** red under-covered / white well-covered dots confirmed visible |
| 11 | Browser visual — UI controls present | **[PASS]** Start/Finish/Camera:orbit buttons, Auto exposure, Brightness slider, Scanner FOV (Normal/Wide) all present |

## Device-connect stdout excerpt (verbatim, abbreviated)

```
[launcher] Running live scan server in a worker process (up to 300s)...
[vulkan_disable_interpose] loaded
[worker] Loading OBScan SDK from: /Applications/CrealityScan.app/Contents/PlugIns/liborbbec_scan.dylib
[worker] obscan_context_create                         -> OB_SCAN_SUCCESS (0)
[worker] obscan_context_get_device_management          -> OB_SCAN_SUCCESS (0)
[worker] obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1
[worker] obscan_context_create_session                 -> OB_SCAN_SUCCESS (0)
[worker] obscan_session_create_scan                    -> OB_SCAN_SUCCESS (0)
[worker] obscan_scan_preview                           -> OB_SCAN_SUCCESS (0)
[worker] obscan_scan_config_combined_exposure_and_laser_current -> OB_SCAN_SUCCESS (0)
[worker][data] msg_id=325189890 (FRAME_READY) data=0x7cfad38900 thread=5538291037024343929
[worker][data] FRAME_READY tracking_status=0 (SUCCESS)
[worker][data] msg_id=325189890 (FRAME_READY) data=0x7cfad39fe0 thread=5538291037024343929
[worker][data] FRAME_READY tracking_status=0 (SUCCESS)
```

## Browser visual (coordinator observations at frame #1067)

- Status: connected; scan: paused (live) after coordinator confirmed
- Frame age: p50 5.2 ms / p95 13.0 ms
- Points: 19894; coverage: 39% covered, 8360 under-covered pts
- Tracking: SUCCESS (0); render: 60.1 fps
- Density colouring: red dots for under-covered areas, white for well-covered — confirmed visible
- Camera: orbit mode button present and selectable
- Controls: Start / Finish / Camera:orbit buttons, Auto exposure checkbox, Brightness slider, Scanner FOV (Normal/Wide)

## Crash sweep

no crash report for 'scan_server' at/after 2026-10-24 03:39:46 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.

## Open questions

- Is the p95 frame age of 13.0 ms representative of steady-state, or elevated due to the browser pausing mid-stream?

## Board

- Row move: pending plan row for this baseline — vendor pipeline confirmed PASS; raw_capture/power-cycling experiments broke nothing; density colouring and all UI controls present with live data.
