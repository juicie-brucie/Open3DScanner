# PLAN #164 — UVC descriptor probe format_index fix — BLOCKED

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: 95218cc719b0145a952262a74de756406f8b10b7
- Target: build/tools/raw_capture/raw_capture (deterministic selection: only copy)
- Other copy: none
- Preflight: FAIL — newest source 'tools/raw_capture/probe_test.cpp' is NEWER than binary 'build/tools/raw_capture/raw_capture' — rebuild (a header change counts; #107: a worker-only rebuild can leave a stale bundle copy).
- Interposer: not reached — preflight blocked before run
- Run start: 2026-10-24 00:54:19

## Command

```
build/tools/raw_capture/raw_capture --probe
```

Not executed — preflight blocked.

## Result

- Exit code: N/A (run not started)
- Verdict: **BLOCKED**
- Raw log: `2026-10-24-plan-uvc-probe-indexfix.raw.log` (same directory)
- Interposer confirmation: ABSENT — stopped at preflight

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Binary newer than all sources | **[FAIL]** `probe_test.cpp` mtime 2026-10-24 00:51:33 > binary mtime 2026-10-24 00:50:50 (43 s stale) |

## Crash sweep

hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 00:54:19 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure or block — observable + one lead

- Observable: `scripts/hw-preflight.sh raw_capture "" build/tools/raw_capture/raw_capture` exited 1: `hw-preflight: FAIL — newest source 'tools/raw_capture/probe_test.cpp' is NEWER than binary 'build/tools/raw_capture/raw_capture'`. Binary mtime 2026-10-24 00:50:50; `probe_test.cpp` mtime 2026-10-24 00:51:33. HEAD is identical to the prior blocked record (95218cc). `descriptor_probe.cpp` mtime 2026-10-24 00:50:06 (pre-dates binary — OK).
- Lead: binary stale — `probe_test.cpp` was touched after the last `cmake --build` of `raw_capture`; the reported rebuild did not advance the binary past the source timestamp.

## Open questions

- Was `cmake --build` actually invoked after `probe_test.cpp` was last written, or was only a partial build (e.g. a different target) run?
- Does `probe_test.cpp` participate in the `raw_capture` build, or is it a test-only translation unit that requires a different CMake target?

## Board

- Row move: none — run did not execute; board state unchanged.
