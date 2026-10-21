# PLAN #71 — attempt 8 (single-session A/B, floor plane) — BLOCKED (pipeline stall after raptor_client #132 captures)

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (synced to origin/main @ `2174a66`)
- Target: build/tools/scan_server/scan_server (build-tree copy, fixed order 2: build/tools/<target>/<target>)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (22:17:05) — not used
- Preflight: `hw-preflight: OK`; pwd OK; no conflicting process; daemon RPCServer(26387,) running; freshness binary 22:17:05 >= newest source `fast_encode.h` 22:06:30
- Interposer: expected `[vulkan_disable_interpose] loaded` (line 2) — present
- Run start: 2026-10-20 23:12:05
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`

## Command

```
OPENSCANNER_PLANE_REMOVE=1 ./build/tools/scan_server/scan_server --live 8787
```

## Result

- Exit code: SIGKILL on worker (SIGTERM then SIGKILL; `obscan_scan_start` never reached)
- Verdict: **BLOCKED**
- Raw log: `2026-10-20-plan71-attempt8-blocked.raw.log` (same directory)

## Observable

`frame_ready_count=0` and continuous `Pipeline.cpp:310 Wait for frame timeout, you can try to increase the wait time! current timeout=100` (239 occurrences over 24 s logged before termination). `obscan_scan_start` was never reached. The A/B could not begin.

```
[worker] frame_ready_count=0
[10/20 23:12:58.950434][warning][2711243][Pipeline.cpp:310] Wait for frame timeout, you can try to increase the wait time! current timeout=100 [**239 logs in 24014ms**]
```

The immediately preceding sessions were `raptor_client --capture` on both ports 10013 and 10010 (the #132 gap-free captures, exit 0, both streams stopped cleanly). The pipeline stall is the same pattern as the earlier blocked #151 run, which also followed raptor_client sessions. This is the recurring device-state disruption: raptor_client sessions leave the daemon's internal stream state in a condition where the OBScan SDK's pipeline cannot receive frames until a device power-cycle.

## Crash sweep

`scripts/hw-postflight.sh plan71-attempt8-blocked '2026-10-20 23:12:05' scan_server` →
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-20 23:12:05 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

- Lead: the pipeline stall after raptor_client sessions is in the daemon's internal stream state — `Pipeline.cpp`'s `Wait for frame timeout` is outside the code wall (`tools/scan_server/**`); device power-cycle is the only known recovery.

## Board

- Row move: #71 stays `HW` — attempt 8 blocked before Phase A could begin. The prerequisite for attempt 9: scan_server must be the first process to access the device after a power-cycle (raptor_client sessions must not precede it in the same boot).
