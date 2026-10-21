# PLAN #151 — device content-cadence run — PASS (device delivers ~34 Hz; publish rate 9.7 Hz; ratio ~3.5:1)

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (synced to origin/main @ `2174a66`)
- Target: build/tools/scan_server/scan_server (build-tree copy, fixed order 2: build/tools/<target>/<target>)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (22:17:05) — not used
- Preflight: `hw-preflight: OK`; pwd OK; no conflicting process; daemon RPCServer(26387,) running; freshness: binary 22:17:05 >= newest source `fast_encode.h` 22:06:30
- Interposer: expected `[vulkan_disable_interpose] loaded` (line 2) — present
- Run start: 2026-10-20 23:05:43
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`; fresh after power-cycle; scan_server run before any raptor_client session this boot

## Command

```
./build/tools/scan_server/scan_server --live 18899
```

Run for ~90 s; SIGTERM then SIGKILL on worker.

## Result

- Exit code: SIGKILL on worker (SIGTERM sent; launcher confirmed, worker did not exit in 5 s; force-killed; `obscan_scan_pause -> OB_SCAN_SUCCESS` logged)
- Verdict: **PASS**
- Raw log: `2026-10-20-plan151-content-cadence.raw.log` (same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` — present, line 2

## Checks

| # | Check | Result |
|---|---|---|
| 1 | Pipeline healthy post-power-cycle: `frame_ready_count > 0` and `obscan_scan_start` reached | **[PASS]** `frame_ready_count=11` then `obscan_scan_start -> OB_SCAN_SUCCESS (0)` at ~5 s |
| 2 | Device content cadence — `FRAME_READY` rate | **[PASS]** 3082 frames over ~90 s = **34.2 Hz** device frame rate |
| 3 | Publish rate (scan_server's tick rate) | **[PASS]** mean 9.73 /s across 19 perf windows (n=49 per window, `rate=9.6–9.8/s`) |
| 4 | Device-to-publish ratio | **[PASS]** 34.2 Hz device / 9.73 Hz publish = **~3.5 device frames per publish tick** — scan_server discards ~2.5 frames per tick; the source is substantially faster than our publish rate |
| 5 | Tracking distribution | **[PASS]** 3082/3082 `SUCCESS` (100%) — static scene, no LOST or FAILURE_MARKERLESS |
| 6 | Cloud mesh update rate | **[PASS]** early phase (~35 s): 5.0–7.4 changes/s (`run_ticks` p50=1–2); settled phase (~55 s onward): 1.4–1.6 changes/s (`run_ticks` p50=6–7, ~720 ms per mesh update cycle) |
| 7 | No crash | **[PASS]** postflight: no crash report |

## Content-cadence findings

| Metric | Observed |
|---|---|
| Device frame rate | **34.2 Hz** (3082 frames / 90 s) |
| Publish rate | **9.73 Hz** (`kLiveTickIntervalMs = 100` ms floor) |
| Device-to-publish ratio | **~3.5 : 1** — the device delivers ~3.5 frames per published tick |
| Cloud mesh update rate (early, moving) | 5.0–7.4 changes/s |
| Cloud mesh update rate (settled, static) | **1.4–1.6 changes/s** (run_ticks p50=6–7, ~720 ms per cycle) |
| Tracking success rate | 100% (3082/3082 SUCCESS, static scene) |

The ~34 Hz device frame rate is consistent with the vendor profile's claim of 24–50 fps at 1920×1200. The scan_server publishes at ~9.7 Hz (the `kLiveTickIntervalMs = 100` floor), consuming one in ~3.5 device frames. The binding limit for publish rate is the 100 ms tick interval, not device throughput — row #151's headroom question is confirmed: the device can deliver substantially more than the current publish rate.

## Crash sweep

`scripts/hw-postflight.sh plan151-content-cadence '2026-10-20 23:05:43' scan_server` →
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-20 23:05:43 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

## Open questions

- The cloud mesh change rate drops from ~6 changes/s (early, cloud still building) to ~1.4 changes/s (settled, static scene). Is the plateau at 1.4 changes/s a property of the static scene (vendor stops updating a settled cloud) or a throughput floor of the mesh getter? Running with active scanner movement would distinguish these.
- The browser's render loop (receipt→paint) and drop rate at 9.7 Hz vs higher rates remain owed (noted in the row's text as a separate browser/operator observation).

## Board

- Row move: #151 stays `HW` (partially) — the device-side content-cadence and rate-distribution deliverable is confirmed (34.2 Hz device, 3.5:1 ratio to publish, 100% tracking success). The browser render-loop observation (receipt→paint, drop rate at higher rates) is still owed per the row text, but is a browser/operator task. The device-side half is done.
