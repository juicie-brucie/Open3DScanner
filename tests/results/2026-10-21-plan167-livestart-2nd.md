# PLAN #167 — live start re-run (2nd batch, device-clean state) — PASS

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (12:53:07) — refreshed only on an app build (#107)
- Preflight: pwd OK; no conflicting process; daemon: RPCServer(76444,) running; freshness: binary 12:53:07 >= newest source main.cpp 12:52:55
- Interposer: expect line 2: `[vulkan_disable_interpose] loaded` — present in all 5 runs
- Run start: 2026-10-21 12:58:20
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`; no raptor_client session since last power-cycle; device in clean state

## Context

First batch (`2026-10-20-plan167-livestart-5reruns.md`): 5 starts, all `count=0` across all
6 probes, all exiting cleanly via exit 2 (no SIGSEGV). The device was in a state left by
prior raptor_client sessions — the enumeration race fix confirmed working (no crash), but
the device-state condition prevented `count=1` from being returned.

This batch follows:
1. The user's power-cycle (from the streaming-retry session)
2. The #141 tick-instrument run (scan_server --live, clean exit with scan_pause)
No raptor_client session has run since the power-cycle.

## Command

Five sequential `scan_server --live` starts, each run for ~20 s then killed:

```
./build/tools/scan_server/scan_server --live 8787
```

## Result

- Exit code: all 5 runs killed by SIGTERM; all reached `obscan_scan_preview` before kill
- Verdict: **PASS — count=1 on first probe in all 5 runs; no crash; no retries**
- Raw log: `2026-10-21-plan167-livestart-2nd.raw.log` (same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` — present, line 2, in all 5 runs

## Checks

| # | Check | Result |
|---|---|---|
| 1 | Run 1: `count=1` on first probe | **[PASS]** `obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1` (log line 69) |
| 2 | Run 2: `count=1` on first probe | **[PASS]** `obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1` (log line 152) |
| 3 | Run 3: `count=1` on first probe | **[PASS]** `obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1` (log line 235) |
| 4 | Run 4: `count=1` on first probe | **[PASS]** `obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1` (log line 318) |
| 5 | Run 5: `count=1` on first probe | **[PASS]** `obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1` (log line 401) |
| 6 | No retries in any run (count=1 on probe 1 of 6) | **[PASS]** all 5 logs show a single `count=` line; the 20 s window showed no second probe call |
| 7 | No SIGSEGV in any run | **[PASS]** postflight: no crash report; all runs reached `obscan_scan_preview` |

## Comparison with first batch

| Batch | Device state | count=0 runs | count=1 runs | SIGSEGV |
|---|---|---|---|---|
| 1 (2026-10-20) | After raptor_client sessions | 5 / 5 | 0 / 5 | 0 (fix working) |
| 2 (2026-10-21, this run) | After power-cycle, no raptor_client | 0 / 5 | 5 / 5 | 0 |

The fix (`WaitForDevice` bounded retry, 6 probes, 4.75 s) prevents the crash in both states.
In the clean state, `count=1` is returned on the first probe with no backoff needed.

## Crash sweep

`scripts/hw-postflight.sh plan167-livestart-2nd '2026-10-21 12:58:20' scan_server` →
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-21 12:58:20 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

## Open questions

- Does `count=0` after a raptor_client session represent the DeviceManager's internal state being set by the RPCServer claim (per `#173`'s finding: `pkill -f RPCServer` untested as a reset), or something else in the GVCP/USB path?
- Are there conditions other than raptor_client that can cause `count=0`? The race was originally attributed to a timing issue on startup, but all 5 first-batch runs showed `count=0` immediately after raptor_client — is raptor_client the sole trigger?

## Board

- Row move: #167 `HW` → `DONE` — the fix prevents SIGSEGV (0/10 crashes across both batches); clean-state starts return `count=1` on the first probe (5/5); poisoned-state starts exit cleanly via exit 2 (5/5, first batch).
