# Row #151 — device-side content cadence/rate distribution — BLOCKED (device enumeration race / crash, not retried)

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: `fc6a081`
- Target: build/tools/scan_server/scan_server (build-tree copy)
- Preflight: pwd OK; no conflicting process; vendor RPCServer daemon running; freshness: binary 2026-10-20 17:26:47 >= newest source main.cpp 17:26:42
- Interposer: n/a — crashed before the interposer-relevant teardown path
- Run start: 2026-10-20 17:39:58
- Device: Creality Raptor Pro, serial `CR0886100SN`, daemon running

## Command

```
./build/tools/scan_server/scan_server --live 8787
```

## Result — BLOCKED

- Exit: worker crashed (`Worker crashed or timed out inside the vendor SDK
  (child killed by signal Segmentation fault: 11)`)
- **Observable, verbatim, in order:**
  ```
  [10/20 17:40:05.799963][info][...][DeviceManager.cpp:188] Device changed! removed: 0, added: 1
  [10/20 17:40:05.799986][info][...][DeviceManager.cpp:15] Current device(s) list: (1)
  [10/20 17:40:05.800011][info][...][DeviceManager.cpp:24] 	- Name: CR-Scan Raptor Pro, PID: 0x0c03, SN/ID: CR0886100SN, Connection: USB3.0
  ...
  [worker] obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=0
  [launcher] Worker crashed or timed out inside the vendor SDK (child killed by signal Segmentation fault: 11).
  ```
  **The lower-level `DeviceManager` log shows the device was detected and
  listed** (`Current device(s) list: (1)`, with the correct name/serial),
  **but the `obscan_device_management_get_device_num` call immediately
  after still returned `count=0`** — a mismatch between what the device
  manager had just logged and what the enumeration API returned,
  immediately followed by the worker crashing. This looks like a timing/
  race condition (the API call landing before the device list update was
  visible to it) rather than a genuine "no device" state, but this agent
  is not asserting that diagnosis — reported as observed only.
- Crash report: `2026-10-20-plan151-device-run-raceloss.crash.ips`, sha256
  `aea4111de556ab74eb4df1dcba0a80f9fc6972046694cd6e8c2a43a414133568`
  (incident `BCE2266D-B46E-4C40-8469-151FDE57845A`)
- Raw log: `2026-10-20-plan151-device-run-raceloss.raw.log` (same directory)
- Not retried, per role.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-20-plan151-device-run-raceloss '2026-10-20 17:39:58' scan_server`:

> hw-postflight: crash report matched by app_name + timestamp. (archived as above)

## Open questions

- Is this the same failure mode as the earlier `count=0` crash seen
  before #153's run today (`2026-10-20-plan153-live-run-attempt1-nodevice.md`),
  now narrowed by this run's evidence to a device-manager/enumeration-API
  timing race rather than a genuine disconnect? Not confirmed — this
  agent is flagging the pattern across two occurrences today, not
  diagnosing the mechanism.
- Given this is the **third** device/daemon-touching anomaly in this
  session's recent run of attempts (the depth-port `raptor_client` hang,
  the IR-capture `raptor_client` hang, and now this enumeration race/
  crash), is something about the daemon/device connection degrading
  across repeated connections within one extended session? Not
  investigated — flagged for whoever looks at this next.

## Board

- Row move I would propose for **#151**: stays `HW` — the device-side
  portion remains unconfirmed; this attempt didn't reach the point of
  collecting any rate data before crashing.
