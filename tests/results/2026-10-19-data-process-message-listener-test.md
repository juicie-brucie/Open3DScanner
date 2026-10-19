# PLAN #2/#19 — data_process_message_listener_test rerun — PASS

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tests/hardware/data_process_message_listener_test/data_process_message_listener_test (explicit argument — preflight's fixed-order search only looks under build/tools/<target>/<target>; this binary lives under build/tests/hardware/<target>/<target>)
- Other copy: none
- Preflight: pwd OK; no conflicting process; freshness: binary 2026-10-18 21:46:44 >= newest source main.cpp 2026-10-17 23:32:03
- Interposer: expect first line: `[launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib`
- Run start: 2026-10-19 16:07:07
- Device: Creality Raptor Pro (CR0886100SN), USB3.0, connected

## Command

```
build/tests/hardware/data_process_message_listener_test/data_process_message_listener_test
```

## Result

- Exit code: `0`
- Verdict: **PASS**
- Raw log: `2026-10-19-data-process-message-listener-test.raw.log` (same directory)
- Interposer confirmation: `[launcher] DYLD_INSERT_LIBRARIES=/Applications/CrealityScan.app/Contents/PlugIns/libMoltenVK.dylib:/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw/build/engine/libvulkan_disable_interpose.dylib` (line 1); `[vulkan_disable_interpose] loaded` printed at line 2; the interposer also intercepted at teardown: `[vulkan_disable_interpose] intercepted vkGetInstanceProcAddr lookup of a create function` / `blocked vkCreateInstance`.

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `add_data(NULL context)` -> `DEADLINE_EXCEEDED` | **[PASS]** |
| 2 | `add_process(NULL context)` -> `DEADLINE_EXCEEDED` | **[PASS]** |
| 3 | `add_data(NULL listener)` -> `INVALID_ARGUMENT` | **[PASS]** |
| 4 | `add_process(NULL listener)` -> `INVALID_ARGUMENT` | **[PASS]** |
| 5 | `add_data(listener_a)` #1 -> `SUCCESS` | **[PASS]** |
| 6 | `add_process(listener_a)` #1 -> `SUCCESS` | **[PASS]** |
| 7 | double `add_data` (no remove) -> `INVALID_ARGUMENT` | **[PASS]** |
| 8 | double `add_process` (no remove) -> `INVALID_ARGUMENT` | **[PASS]** |
| 9 | `remove_data(listener_a)` -> `SUCCESS` | **[PASS]** |
| 10 | `remove_data` again (empty) -> `LISTENER_NOT_FOUND` | **[PASS]** |
| 11 | `remove_process(listener_a)` -> `SUCCESS` | **[PASS]** |
| 12 | `remove_process` again (empty) -> `LISTENER_NOT_FOUND` | **[PASS]** |
| 13 | `add_data` after remove -> `SUCCESS` | **[PASS]** |
| 14 | `add_process` after remove -> `SUCCESS` | **[PASS]** |
| 15 | `remove_data(NULL context)` / `remove_process(NULL context)` -> `DEADLINE_EXCEEDED` | **[PASS]** |

`[worker] g_failures=0`, `worker exit code: 0` — clean teardown
(`obscan_context_destroy() -> OB_SCAN_SUCCESS (0)`), no crash signal.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-19-data-process-message-listener-test '2026-10-19 16:07:07' data_process_message_listener_test`:

> no crash report for 'data_process_message_listener_test' at/after 2026-10-19 16:07:07 — safe to record 'no crash report (checked DiagnosticReports for data_process_message_listener_test at/after the run start)'.

## Open questions

- PLAN.md row #19 describes this target as running "in-process (no worker,
  interposer or timeout)"; this run's own output shows the launcher/worker/
  interposer pattern (same as device_message_listener_test). Is row #19's
  description describing a different build or a different aspect of the
  test than what this run observed? Not resolved here.

## Board

- Row move: none proposed. PLAN #2/#19 already `DONE`; this is a clean
  reproduction with no new signal. Dispatcher may want to attach this run as
  a second confirmation if useful.
