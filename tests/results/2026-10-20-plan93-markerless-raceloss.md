# Row #93 — the markerless-stretch HUD observation — BLOCKED before reaching tracking, 4th occurrence of the device-enumeration race today

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: `fc6a081`
- Target: build/tools/scan_server/scan_server (build-tree copy)
- Preflight: pwd OK; no conflicting process; vendor RPCServer daemon running; freshness: binary 2026-10-20 17:26:47 >= newest source main.cpp 17:26:42
- Interposer: n/a — crashed before the interposer-relevant teardown path
- Run start: 2026-10-20 18:08:15
- Device: Creality Raptor Pro, serial `CR0886100SN`, daemon running
- Scene: static, scanner held in place, user ready to occlude the lens
  briefly on cue (per this row's confirmed-sufficient acceptance — no
  scanner movement needed, only a brief occlusion action)

## Intent for this attempt (not reached)

Start `scan_server --live`, confirm the HUD renders live, then have the
user briefly cover the lens to starve the marker-count guard
(`cached_detect_3d_markers`, per `docs/recon/findings/tracking-mechanism.md`
§1) and produce a genuine `LOST`→`FAILURE_MARKERLESS` stretch, observed
via the coordinator's browser pane watching `trackingText()`'s rendered
HUD line for the absence of any "markerless window k/3"-style suffix.

## What actually happened — blocked before any of that

**This is the fourth occurrence today of the exact same device-
enumeration race/crash** already seen three times
(`2026-10-20-plan151-device-run-raceloss.md`,
`2026-10-20-plan151-device-retry2-raceloss.md`,
`2026-10-20-plan71-attempt6-raceloss.md`), reproduced identically once
again:

```
[worker]  DeviceManager.cpp:188  Device changed! removed: 0, added: 1
[worker]  DeviceManager.cpp:15   Current device(s) list: (1)
[worker]  DeviceManager.cpp:24   - Name: CR-Scan Raptor Pro, PID: 0x0c03, SN/ID: CR0886100SN, Connection: USB3.0
...
[worker] obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=0
[launcher] Worker crashed or timed out inside the vendor SDK (child killed by signal Segmentation fault: 11).
```

This crashed during the same early device-enumeration step every
`--live` session goes through, before the WS server ever came up and
before any tracking status could be observed. Nothing about this row's
own test (the HUD's `trackingText()` rendering) was exercised.

- Crash report: `2026-10-20-plan93-markerless-raceloss.crash.ips`, sha256
  `524bcdc6d2e92a57d847ef3e76230c4b9638ef6fe42f4ba56f7c460c2c6c4aaa`
  (incident `A32D7106-10C1-44E1-948C-160683C470D6`)
- Raw log: `2026-10-20-plan93-markerless-raceloss.raw.log`
- **Not retried**, per the no-retry rule — this is now the fourth
  reproduction of this exact failure today, across three different rows
  (#151 twice, #71, now #93), independent of what each attempt was
  actually trying to test.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-20-plan93-markerless-raceloss '2026-10-20 18:08:15' scan_server`:

> hw-postflight: crash report matched by app_name + timestamp. (archived as above)

## Open questions

- Same as the three prior occurrences. With this now at 4-for-4 across
  essentially every fresh `scan_server --live` startup attempted since
  mid-session today (two of the four following an independently-confirmed
  -good physical connection check), this looks less like intermittent
  flakiness and more like a near-deterministic startup race that should
  be reproducible by a builder without hardware in hand, if the race is
  timing-based rather than device-state-based. Worth prioritizing.

## Board

- Row move I would propose for **#93**: stays `HW` — not reached. No
  further `scan_server --live` hardware attempts are likely to succeed
  until this race is addressed; recommend holding all remaining
  device-touching rows (#93, #71, #151, and anything else needing a fresh
  `--live` startup) until it's fixed or a reliable workaround (e.g. a
  retry-with-backoff inside `scan_server` itself) is in place.
