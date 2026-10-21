# PLAN #167 — the device-enumeration race re-run, 5 consecutive live starts — the crash is gone; enumeration itself still never succeeds in this window

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (synced to origin/main @ `2174a66`)
- Target: build/tools/scan_server/scan_server (build-tree copy, fixed order 2: build/tools/<target>/<target>)
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (22:17:05) — not used this run
- Preflight: `hw-preflight: OK`; pwd OK; no conflicting process; daemon RPCServer(26387,) running; freshness: binary 22:17:05 >= newest source `fast_encode.h` 22:06:30
- Interposer: expected `[vulkan_disable_interpose] loaded` (line 2) — present in all 5 runs
- Run start: 2026-10-20 22:20:00
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`, connection confirmed present (DeviceManager logs it in all 5 runs: `Current device(s) list: (1)`)

## Command

```
./build/tools/scan_server/scan_server --live 18899   # x5, each observed up to 20s, SIGTERM only if still alive after 20s
```

(First attempt at a 5-start sequence used an 8s observation window per start and SIGTERM'd every run before it reached its own decision — a self-inflicted methodology error, not a hang; corrected to a 20s window and re-run cleanly. Only the corrected 5 runs are reported below.)

## Result

- Exit code: `2` for all 5 runs (none required SIGTERM; all exited on their own within the window)
- Verdict: **the crash is fixed (5/5); the underlying enumeration race is unresolved (5/5)**
- Raw log: `2026-10-20-plan167-livestart-5reruns.raw.log` (same directory, all 5 runs concatenated)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` — present, line 2, all 5 runs

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | No SIGSEGV across 5 consecutive live starts (the original row #167 symptom) | **[PASS]** all 5 exited with code 2 ("Worker exited normally with code 2"); postflight found no crash report |
| 2 | Each run's device-enumeration sequence completes within the bounded retry (no hang) | **[PASS]** each run logs `device enumeration settled after 6 probe(s), 4750 ms of backoff.` |
| 3 | `get_device_num` eventually returns a nonzero count within the 4.75 s budget, given `DeviceManager` logs the device present (`Current device(s) list: (1)`) and the RPC connection state reaches `1` ~100 ms after that | **[FAIL]** 5/5 runs: all 6 probes returned `OB_SCAN_SUCCESS, count=0`; final line every time: `No device detected after 6 probe(s) over 4750 ms (budget 4750 ms) — nothing to stream.` |
| 4 | A no-device outcome exits cleanly instead of reaching the vendor's crash-prone teardown | **[PASS]** `[launcher] Worker exited normally with code 2.` in all 5 runs |

## Crash sweep

`scripts/hw-postflight.sh plan167-5starts-reattempt '2026-10-20 22:20:00' scan_server` →
`hw-postflight: no crash report for 'scan_server' at/after 2026-10-20 22:20:00 — safe to record 'no crash report (checked DiagnosticReports for scan_server at/after the run start)'.`

## On failure or block — observable + one lead

- Observable: in all 5 runs, `DeviceManager.cpp:188` logs `Device changed! removed: 0, added: 1` and `Current device(s) list: (1)` roughly 100 ms before `Ethernet.cpp:244` logs `rpc client connection state:1`; every subsequent `obscan_device_management_get_device_num` call for the rest of the 4750 ms budget still returns `OB_SCAN_SUCCESS, count=0`, and every run ends in `No device detected after 6 probe(s) over 4750 ms (budget 4750 ms) — nothing to stream.` — a clean, documented exit(2), not a crash, but the device is never actually usable in any of the 5 runs.
- Lead: the gap between `DeviceManager`'s own device list and what `get_device_num` reports — and whether 4750 ms is simply too short a budget for this daemon/device pairing — is in `tools/scan_server/device_probe.h`'s probe/backoff schedule, not in `main.cpp`'s call site.

## Open questions

- Is 4750 ms (6 probes, 250 ms doubling) long enough for this device to ever report a nonzero count, or does `get_device_num` lag `DeviceManager`'s own list indefinitely until some other condition is met?
- Is the same physical connection actually usable during this window (the same open question #167's own evidence table left for 3 of its 4 original crash records) — none of these 5 runs independently re-confirmed the connection mid-run the way `plan151-device-retry2-raceloss` did.

## Board

- Row move: #167 stays `HW` — the crash this row targeted (SIGSEGV on a transient `count=0` reaching vendor teardown) is confirmed fixed, 5/5, with a clean `exit(2)` instead. But the row's deliverable text also calls for "a live start re-run" implying a working device session, and that is not what was observed: 5/5 runs ended in "no device detected," not a successful stream. The row cannot move to `DONE` on this evidence; whether the remaining gap is a new row or #167 reopened is for the dispatcher/builder to decide.
