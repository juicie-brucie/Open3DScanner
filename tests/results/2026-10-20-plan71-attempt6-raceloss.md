# PLAN #71 — attempt 6 (mid-session toggle A/B, using #158's runtime command) — BLOCKED before reaching the plane-toggle logic, same device-enumeration race

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: `fc6a081`
- Target: build/tools/scan_server/scan_server (build-tree copy)
- Preflight: pwd OK; no conflicting process; vendor RPCServer daemon running; freshness: binary 2026-10-20 17:26:47 >= newest source main.cpp 17:26:42
- Interposer: n/a — crashed before the interposer-relevant teardown path
- Run start: 2026-10-20 18:00:59
- Device: Creality Raptor Pro, serial `CR0886100SN`, daemon running
- Static scene: same setup as attempt 5, user confirmed untouched/still
  static immediately before this attempt (explicitly re-confirmed, not
  assumed, given time had passed since the last confirmation)

## Intent for this attempt (not reached)

This was meant to be the first #71 attempt using #158's new mid-session
`set_plane_removal` WS command (confirmed landed in the real `--live`
path during this session's `ungated-smoke-batch3` investigation) — start
`scan_server --live` with plane-removal **off**, record a baseline
per-tick point-count series, send `{"cmd":"set_plane_removal","auto":true,
"turntable":true,"manual":"0,0,1,-0.33"}` over a transient WS connection
partway through, and compare the per-tick increments **within one
continuous session** on one held-static scene — avoiding every prior
attempt's two-separate-process confound entirely.

## What actually happened — blocked before any of that

The run hit the **same device-enumeration race/crash already seen twice
today** (`2026-10-20-plan151-device-run-raceloss.md`,
`2026-10-20-plan151-device-retry2-raceloss.md`) — this is now the
**third** occurrence, reproduced exactly:

```
[worker]  DeviceManager.cpp:188  Device changed! removed: 0, added: 1
[worker]  DeviceManager.cpp:15   Current device(s) list: (1)
[worker]  DeviceManager.cpp:24   - Name: CR-Scan Raptor Pro, PID: 0x0c03, SN/ID: CR0886100SN, Connection: USB3.0
...
[worker] obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=0
[launcher] Worker crashed or timed out inside the vendor SDK (child killed by signal Segmentation fault: 11).
```

This crashed during the same early device-enumeration step every
`--live` session goes through, **before reaching the scan-start/
plane-toggle code this attempt was actually testing**. Nothing about
#158's new command or the mid-session-toggle approach was exercised —
this attempt never got far enough to test what it set out to test.

- Crash report: `2026-10-20-plan71-attempt6-raceloss.crash.ips`, sha256
  `f1a6911e130b1b0f174e6f34b78d58c46101d75631f443df1a2bda89a76d534e`
  (incident `78D87B89-B5FA-4E51-A01E-B5B5BF1C3CE3`)
- Raw log: `2026-10-20-plan71-attempt6-raceloss.raw.log`
- **Not retried**, per the no-retry rule — this is also the third
  occurrence of this exact failure today, now considered a confirmed,
  highly reproducible blocker rather than something worth a fourth
  attempt without a fix.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-20-plan71-attempt6-raceloss '2026-10-20 18:00:59' scan_server`:

> hw-postflight: crash report matched by app_name + timestamp. (archived as above)

## Open questions

- Same as the two `#151` race occurrences — this is now 3-for-3 today on
  fresh `scan_server --live` startups, independent of what the run was
  actually trying to test. This looks like a priority item: whatever
  causes `get_device_num` to return `count=0` immediately after the
  device manager logs the device as present is now blocking multiple
  rows (#71, #151) from even starting, not just one.

## Board

- Row move I would propose for **#71**: stays `HW` — attempt 6 is
  effectively void; the mid-session-toggle approach itself was never
  exercised. Recommend this device-enumeration race be fixed or worked
  around before any further #71/#151 hardware attempts, since it's now
  blocking both rows identically and repeatedly.
