# PLAN #8/#10 — device_message_listener_test rerun — PASS

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tests/hardware/device_message_listener_test/device_message_listener_test (explicit argument — preflight's fixed-order search only looks under build/tools/<target>/<target>; this binary lives under build/tests/hardware/<target>/<target>, so the path was passed explicitly)
- Other copy: none
- Preflight: pwd OK; no conflicting process; freshness: binary 2026-10-18 21:46:44 >= newest source main.cpp 2026-10-17 23:32:03
- Interposer: expect first line: `[launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib`
- Run start: 2026-10-19 16:04:20
- Device: Creality Raptor Pro (CR0886100SN), USB3.0, connected

## Command

```
build/tests/hardware/device_message_listener_test/device_message_listener_test
```

## Result

- Exit code: `1`
- Verdict: **PASS** — all 7 explicit `[worker][PASS]` checks printed; exit 1 is
  a trailing `SIGSEGV` in the vendor SDK's device-connect callback, the same
  known failure mode already tracked as PLAN #11 (see
  `2026-10-17-plan8-device-message-listener-crash-analysis.md`), not a failed
  check and not the 180s watchdog (`worker did not exit cleanly: child killed
  by signal Segmentation fault: 11`, total run time ~4s).
- Raw log: `2026-10-19-plan8-device-message-listener-test.raw.log` (same directory)
- Interposer confirmation: `[launcher] DYLD_INSERT_LIBRARIES=/Applications/CrealityScan.app/Contents/PlugIns/libMoltenVK.dylib:/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw/build/engine/libvulkan_disable_interpose.dylib` (line 1); `[vulkan_disable_interpose] loaded` (line 2).

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `add(NULL handle, listener)` -> `OB_SCAN_ERROR_DEADLINE_EXCEEDED` | **[PASS]** `[worker][PASS] add(NULL handle, listener) == DEADLINE_EXCEEDED` |
| 2 | `add(handle, NULL listener)` -> `OB_SCAN_ERROR_INVALID_ARGUMENT` | **[PASS]** `[worker][PASS] add(handle, NULL listener) == INVALID_ARGUMENT` |
| 3 | `add(handle, listener_a)` #1 -> `OB_SCAN_SUCCESS` | **[PASS]** `[worker][PASS] add(handle, listener_a) #1 == SUCCESS` |
| 4 | double add without remove -> `OB_SCAN_ERROR_INVALID_ARGUMENT` | **[PASS]** `[worker][PASS] double add (no remove) == INVALID_ARGUMENT` |
| 5 | `remove(handle, listener_a)` -> `OB_SCAN_SUCCESS` | **[PASS]** `[worker][PASS] remove(handle, listener_a) == SUCCESS` |
| 6 | `remove(NULL handle, listener)` -> `OB_SCAN_ERROR_DEADLINE_EXCEEDED` | **[PASS]** `[worker][PASS] remove(NULL handle, listener) == DEADLINE_EXCEEDED` |
| 7 | add after remove (OQ-3) -> `OB_SCAN_SUCCESS` | **[PASS]** `[worker][PASS] add after remove == SUCCESS (OQ-3 cleared slot)` |

## Crash sweep

`scripts/hw-postflight.sh 2026-10-19-plan8-device-message-listener-test '2026-10-19 16:04:20' device_message_listener_test` matched by app_name + timestamp:

- Crash report: [`2026-10-19-plan8-device-message-listener-test.crash.ips`](./2026-10-19-plan8-device-message-listener-test.crash.ips) from `device_message_listener_test-2026-10-19-160517.ips`, sha256 `88ee0f70b15a319bb7a9dc85c850d0262f3a19b8f47a32ef4270f52bcf2d42a9` (incident `09C81182-2E05-479B-969E-B217B76E1847`)

Faulting thread's top frames (`device::OrbbecDevice::get_active_device()` ->
`OBDeviceManagement::update_device_info` -> `update_camera_status` -> device-
connect callback) match the stack already analysed in
`2026-10-17-plan8-device-message-listener-crash-analysis.md` for PLAN #11 —
reported here as an observable match, not re-diagnosed.

## Open questions

- Does this crash reproduce on every run that exercises the OQ-3 re-add path,
  or only intermittently? (This run is a second data point; #11 owns the answer.)
- Is the NULL dereference in `get_active_device()` triggered by callback
  re-entrancy during the device re-create, or a pre-existing vendor-side
  inconsistency the re-add merely exposes? (Open per the #11 analysis; not
  settled by this run.)

## Board

- Row move: none proposed. PLAN #8/#10 are already `DONE`; this run is a
  rerun that reproduces the same already-tracked #11 crash signature on a
  second occasion. Dispatcher may want to note the reproduction against #11's
  row, but that is the dispatcher's call, not mine.
