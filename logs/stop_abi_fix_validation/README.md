# Validation: restored `obscan_scan_stop` ABI + mesh-read-after-pause

These two runs are the first hardware confirmation that
`obscan_scan_stop`'s real native shape is
`(scan, progress_cb, status_cb, is_block)` — not the 2-arg
`(scan, is_block, status_cb)` shape that briefly landed on `main`
earlier this session (see git history on `obscan_api.h` around this
time, and `logs/branch_claude-clever-albattani-cebx0e/` for how that
regression was caught) — and that `obscan_scan_get_realtime_model_mesh`
must be read after `obscan_scan_pause`, not during active scanning.

## Result: real data, for the first time

Both runs produced a genuine reconstructed mesh via
`obscan_scan_get_realtime_model_mesh`, read right after
`obscan_scan_pause` succeeded:

- `live_capture_test_run1_90s_timeout.log`: 5913 vertices, 4037 triangles.
- `live_capture_test_run2_180s_timeout.log`: 6239 vertices, 4220 triangles.

`obscan_scan_stop` and `obscan_session_destroy` both returned
`OB_SCAN_SUCCESS` in both runs (not `OB_SCAN_ERROR_MULTI_CALLED`, which
every prior run got with the broken 2-arg `stop`). The native logs
(`native_scan_log_run1.txt` / `run2.txt`) show the complete, real
device-release sequence: `"stop progress 0.01 ... 1.0"`,
`"change to stop success !"`, `"destory OBScanContextHandle success !"`,
and `"rpc client destroyed"` for all four client ports (RGB, IR, IMU,
Depth Camera).

## New problem uncovered: a second, Vulkan-unrelated hang

Both runs still ended in the worker being force-killed by
`RunSelfAsWorker`'s timeout (90s in run 1, 180s in run 2 — bumped
specifically to test this). In both native logs, the *entire* logged
teardown sequence above completes in well under a second, ending at
`"Streamer deInit done."` — and neither log contains a single
`VK_KHR`/`VK_EXT`/`"using metallib"` line, i.e. the known
SuperPoint/Vulkan path (see `engine/src/vulkan_disable_interpose.c`)
was never even entered this time.

So this is a *different* hang from the one the Vulkan interposer
targets: something after `"Streamer deInit done."`, inside
`obscan_context_destroy()`, produces zero log output and blocks for
180+ seconds — and it only appeared once there was real scan data
(markers detected, real mesh) to have processed, which no earlier
successful run had. Leading theory, not yet verified: some AI
postprocessing/cache-teardown step specific to a non-empty scan result,
unrelated to Vulkan/ncnn. Needs its own investigation; the earlier
"Vulkan fix confirmed" README note has been corrected to reflect that
it was never actually validated end-to-end (prior "fast" runs were
fast only because the broken `stop()` caused `MULTI_CALLED` to refuse
before `context_destroy` ever reached real work).
