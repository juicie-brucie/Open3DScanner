# 2026-10-17 — scan_server live acceptance (PLAN #25 / OQ-S5)

**Target program:** `build/tools/scan_server/scan_server` (live mode, self-forked worker)
**Worktree:** `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`, branch `hw/next` (clean at start)
**Command:**
```
./build/tools/scan_server/scan_server --live 8787 "" 30
```
**Exit code:** `0`
**Raw log:** `tests/results/2026-10-17-scan-server-live-acceptance.log` (full stdout+stderr, 1383 lines)

## Pre-run checks

- Confirmed `pwd` = `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`, branch `hw/next`, working tree clean.
- `ps aux | grep -i -E "scan_server|device_message_listener|obscan"` returned no matching process — no other SDK user attached (rule 7).
- Did not set `DYLD_INSERT_LIBRARIES` myself; the binary's internal `RunLiveMode` injects it into the worker via `setenv` (`tools/scan_server/main.cpp` lines 735–750), as described in the task brief — this does not trip the "never set DYLD_INSERT_LIBRARIES" guard because I am not the one setting it.
- Did not pkill or signal the process; let the 30s session plus its internal pause/mesh/stop/destroy teardown run to completion unattended.

## Launcher/interpose banner check (rule 6, per task-specific guidance)

Line 2 of stdout: `[vulkan_disable_interpose] loaded` — present, confirming the interposer is active in the worker process, as instructed for this binary (it doesn't print a `[launcher] DYLD_INSERT_LIBRARIES=...` line itself since the injection is internal `setenv`, not a wrapper script).

## Checks

| Check | Result |
|---|---|
| `[vulkan_disable_interpose] loaded` banner present | [PASS] |
| SDK library loaded (`liborbbec_scan.3.5.3.dylib`) without symbol-resolution error | [PASS] |
| `obscan_context_create` → SUCCESS | [PASS] |
| Device enumerated (CR-Scan Raptor Pro, SN CR0886100SN) | [PASS] |
| `obscan_scan_preview` / `frame_ready_count=10` / `obscan_scan_start` → SUCCESS | [PASS] |
| Per-tick status line present (`[worker] tick N/300: color_cloud num_points=... raw_cloud num_points=...`) | [PASS] |
| **`color_cloud num_points` > 0 during an active (not-yet-paused) tick** | [PASS] |
| `obscan_scan_pause` → SUCCESS | [PASS] |
| `obscan_scan_get_realtime_model_mesh` → SUCCESS (final mesh, 6112 vertices, 4331 triangles) | [PASS] |
| `obscan_scan_stop` → SUCCESS | [PASS] |
| `obscan_session_destroy` → SUCCESS | [PASS] |
| `obscan_context_destroy` → SUCCESS | [PASS] |
| Worker exit code 0 / launcher reports "Worker exited normally with code 0" | [PASS] |
| No "missing expected symbol" / symbol-resolution failure for `obscan_scan_get_model_point_color_clouds` or `model_clouds_release` | [PASS] (not observed — no such error string appears anywhere in the log) |
| Crash report for this run in `~/Library/Logs/DiagnosticReports/` | Not applicable — run exited cleanly (code 0); checked the directory and the newest `scan_server`-named report is none (latest entries are unrelated `live_capture_test`/`smoke_test` reports from Oct 16, before this run's window of 23:34:53–23:35:38 on Oct 17) |

## Verdict: PASS

OQ-S5 is resolved in the affirmative: `color_cloud num_points` goes nonzero during active (not-yet-paused) ticks, not just in the final post-pause mesh broadcast. This confirms PLAN #25's wiring (polling `obscan_scan_get_model_point_color_clouds` once per tick, PR #67) delivers real live geometry while scanning is still in progress, contradicting the old PLAN #23 behavior where only the final mesh carried nonzero points.

## Per-tick log excerpt (color_cloud / raw_cloud point counts over the run)

```
[worker] tick 0/300: color_cloud num_points=0 raw_cloud num_points=0 (max seen so far: 0), clients=0, paused=0
[worker] tick 50/300: color_cloud num_points=292 raw_cloud num_points=292 (max seen so far: 292), clients=0, paused=0
[worker] tick 100/300: color_cloud num_points=340 raw_cloud num_points=340 (max seen so far: 341), clients=0, paused=0
[worker] tick 150/300: color_cloud num_points=442 raw_cloud num_points=442 (max seen so far: 442), clients=0, paused=0
[worker] tick 200/300: color_cloud num_points=867 raw_cloud num_points=867 (max seen so far: 868), clients=0, paused=0
[worker] tick 250/300: color_cloud num_points=883 raw_cloud num_points=883 (max seen so far: 885), clients=0, paused=0
```

Note: at tick 0 (immediately after `obscan_scan_start`, before any real scanning motion) both counters read 0 — expected, since no points have been captured yet. From tick 50 onward, `color_cloud num_points` is nonzero and tracks `raw_cloud num_points` closely (equal or within 1–2 of the running max) at every sampled tick, all while `paused=0`. No tick after the first shows `color_cloud num_points=0` while scanning was active. The session was paused (`obscan_scan_pause`) shortly after tick 250 (no further periodic tick line printed before the pause, consistent with the per-50-tick sampling cadence and `session_seconds=30` × ~10 Hz ≈ 300 total ticks).

## Load-time symbol check

No "missing expected symbol", "symbol not found", `dlsym` failure, or similar error string appears anywhere in the log for `obscan_scan_get_model_point_color_clouds` or `model_clouds_release`. The SDK loaded cleanly from `/Applications/CrealityScan.app/Contents/PlugIns/liborbbec_scan.dylib` → resolved `liborbbec_scan.3.5.3.dylib`, and every per-tick call to the color-cloud getter produced a plausible, monotonically-growing point count (0 → 292 → 340 → 442 → 867 → 883) rather than an error, consistent with both export names resolving as expected. This is evidence of absence (no failure observed in this run) for the rename-risk PLAN #25 flagged, not a code-level confirmation of the export names.

## Teardown / vulkan interpose detail

At shutdown, the interposer intercepted and blocked `vkCreateInstance` as designed (`[vulkan_disable_interpose] blocked vkCreateInstance`, `vkCreateInstance failed -3`), and `obscan_context_destroy` still returned `OB_SCAN_SUCCESS (0)` — consistent with the documented purpose of this interposer (prevent the vendor SDK's Vulkan teardown path from crashing/hanging, per `tools/live_capture_test/main.cpp` comments referenced in `scan_server/main.cpp`).

## Open questions

- None outstanding for OQ-S5 itself — resolved affirmatively by this run.
- The exact semantics of `model_clouds_release` (whether/when it's called per tick inside the SDK call wrapper) were not directly observable from stdout; this run only confirms the getter call succeeds and returns growing nonzero counts, not the release path specifically. If that needs separate confirmation, it would require source inspection (out of scope here) or additional instrumentation — not something this run's black-box stdout evidence can settle.
