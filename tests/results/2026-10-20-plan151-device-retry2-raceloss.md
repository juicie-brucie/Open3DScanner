# Row #151 — device-side run, second attempt after a confirmed-good connection check — same race/crash, not retried further

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: `fc6a081`
- Target: build/tools/scan_server/scan_server (build-tree copy)
- Preflight: pwd OK; no conflicting process; vendor RPCServer daemon running; freshness: binary 2026-10-20 17:26:47 >= newest source main.cpp 17:26:42
- Interposer: n/a — crashed before the interposer-relevant teardown path
- Run start: 2026-10-20 17:58:09
- Device: Creality Raptor Pro, serial `CR0886100SN`, daemon running
- Context: retried after the user confirmed the physical connection
  directly (following the first occurrence of this exact failure,
  `2026-10-20-plan151-device-run-raceloss.md`).

## Command

```
./build/tools/scan_server/scan_server --live 8787
```

## Result — the identical race/crash, reproduced exactly

- **Observable, verbatim, in order — matches the first attempt exactly:**
  ```
  [10/20 17:58:18.715417][info][...][DeviceManager.cpp:188] Device changed! removed: 0, added: 1
  [10/20 17:58:18.715435][info][...][DeviceManager.cpp:15] Current device(s) list: (1)
  [10/20 17:58:18.715443][info][...][DeviceManager.cpp:24] 	- Name: CR-Scan Raptor Pro, PID: 0x0c03, SN/ID: CR0886100SN, Connection: USB3.0
  ...
  [worker] obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=0
  [launcher] Worker crashed or timed out inside the vendor SDK (child killed by signal Segmentation fault: 11).
  ```
- Same sequence as the first occurrence: the device manager logs the
  device as present and listed, immediately followed by
  `get_device_num` returning `count=0` anyway, immediately followed by
  the worker crashing. **This is now 2-for-2, reproduced exactly, after
  an independently-confirmed-good physical connection** — this rules out
  a loose/flaky physical connection as the explanation and strengthens
  the earlier hypothesis that this is a genuine timing race between the
  device-manager's internal state and the `get_device_num` call, not an
  environmental issue.
- Crash report: `2026-10-20-plan151-device-retry2-raceloss.crash.ips`,
  sha256 `4cc4261ac5c80294e7e2aab2bb0452edd8a5798542e1427e59080411684049dd`
  (incident `B4F21A29-4C39-4927-87B1-4421919ABD94`)
- Raw log: `2026-10-20-plan151-device-retry2-raceloss.raw.log`
- **Not retried further**, per the no-retry rule and the coordinator's
  explicit "stop after one retry" instruction.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-20-plan151-device-retry2-raceloss '2026-10-20 17:58:09' scan_server`:

> hw-postflight: crash report matched by app_name + timestamp. (archived as above)

## Open questions

- This reproduces exactly, twice, with a confirmed-good physical
  connection in between — strong evidence for a software-level race
  rather than hardware/cabling flakiness. Worth a builder-level look at
  the timing between `DeviceManager`'s internal device-list update and
  `obscan_device_management_get_device_num`'s own read of that state.
- Both occurrences happened on an otherwise-normal `scan_server --live`
  startup with no other SDK user — not obviously tied to session history
  or prior connections (unlike the `raptor_client` capture hangs, which
  happened after several prior connections in the same session).

## Board

- Row move I would propose for **#151**: stays `HW` — the device-side
  content-cadence/rate-distribution data still hasn't been collected;
  this specific startup race needs to be fixed or worked around (e.g. a
  retry-with-backoff on `get_device_num` inside `scan_server` itself,
  builder's call) before this row's device-side observation can be
  completed at all.
