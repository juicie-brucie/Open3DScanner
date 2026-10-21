# PLAN #151 — device content-cadence run — BLOCKED (scan never started: pipeline frame timeout after raptor_client session)

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (synced to origin/main @ `2174a66`)
- Target: build/tools/scan_server/scan_server (build-tree copy, fixed order 2: build/tools/<target>/<target>)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (22:17:05) — not used
- Preflight: `hw-preflight: OK`; pwd OK; no conflicting process; daemon RPCServer(26387,) running; freshness: binary 22:17:05 >= newest source `fast_encode.h` 22:06:30
- Interposer: expected `[vulkan_disable_interpose] loaded` (line 2) — present
- Run start: 2026-10-20 23:00:31
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`

## Command

```
./build/tools/scan_server/scan_server --live 18899
```

(Run for 95 s before SIGTERM)

## Result

- Exit code: SIGKILL (SIGTERM sent, worker did not exit, force-killed)
- Verdict: **BLOCKED**
- Raw log: `2026-10-20-plan151-content-cadence-blocked.raw.log` (same directory)
- Interposer confirmation: present

## Observable

Enumeration succeeded (`count=1`) but the SDK pipeline never delivered any frames. The log shows continuous `Pipeline.cpp:310 Wait for frame timeout, you can try to increase the wait time! current timeout=100` — 478 occurrences over 48 s. `frame_ready_count=0` when the 10-frame threshold check fired. `obscan_scan_start` was never reached (it requires `frame_ready_count >= 10`). No `[perf]` lines, no `FRAME_READY` lines, no tracking lines.

Verbatim end state:
```
[worker] frame_ready_count=0
[10/20 23:02:15.270602][warning][2697492][Pipeline.cpp:310] Wait for frame timeout, you can try to increase the wait time! current timeout=100 [**478 logs in 48064ms**]
```

The immediately preceding session was a `raptor_client --capture` run on port 10013 (exit 1, frame budget reached); before that, `raptor_client --live` on both 10013 and 10010 (exit 0, frames streaming). The pipeline-level frame timeout is consistent with the SDK's internal state being disrupted by those raptor_client sessions holding the daemon's streaming state.

## Crash sweep

`scripts/hw-postflight.sh plan151-content-cadence-blocked '2026-10-20 23:00:31' scan_server` →
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-20 23:00:31 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

## Lead

The SDK pipeline's frame-delivery stall after a prior `raptor_client` session — `Pipeline.cpp`'s `Wait for frame timeout` — is outside this agent's wall; device state recovery (power-cycle or a specific SDK teardown sequence) is the likely prerequisite.

## Board

- Row move: #151 stays `HW` — the device run for content cadence and rate distribution was not reached; the pipeline never delivered frames. A fresh device state (power-cycle) is the prerequisite for a re-attempt.
