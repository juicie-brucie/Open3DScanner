# PLAN #181 — helper claim + capture run (claimed-by-helper check) — BLOCKED

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture
- Other copy: none
- Preflight: FAIL — newest source `tools/raw_capture/descriptor_probe.cpp` is NEWER than binary `build/tools/raw_capture/raw_capture` (#107: a worker-only rebuild can leave a stale bundle copy)
- Interposer: not reached — blocked at preflight
- Run start: 2026-10-24 06:15:47

## Command

```
scripts/hw-preflight.sh raw_capture
```

## Result

- Exit code: `1`
- Verdict: **BLOCKED**
- Raw log: `2026-10-24-plan181-helper-claim-capture-blocked.raw.log` (same directory)
- Interposer confirmation: not reached — blocked at preflight

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | binary newer than newest source in target directory | **[FAIL]** `hw-preflight: FAIL — newest source 'tools/raw_capture/descriptor_probe.cpp' is NEWER than binary 'build/tools/raw_capture/raw_capture' — rebuild (a header change counts; #107: a worker-only rebuild can leave a stale bundle copy).` |

No helper install, claim-status check, ioreg read, or capture was attempted — the preflight gate for `raw_capture` failed before any of those steps.

## Crash sweep

hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 06:15:47 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure or block — observable + one lead

- Observable: `scripts/hw-preflight.sh raw_capture` exited 1 with: `hw-preflight: FAIL — newest source 'tools/raw_capture/descriptor_probe.cpp' is NEWER than binary 'build/tools/raw_capture/raw_capture' — rebuild (a header change counts; #107: a worker-only rebuild can leave a stale bundle copy).`
- Lead: likely `tools/raw_capture/descriptor_probe.cpp` was changed (or merged in) since the last build of `build/tools/raw_capture/raw_capture`; a rebuild of the `raw_capture` target is needed before this card can run.

## Open questions

- Does the pending `descriptor_probe.cpp` change affect the `--capture --stream depth` path this card needs, or is it isolated to the `--probe`/`--table` path?
- Once rebuilt, do the helper claim + `OPENSCANNER_UVC_IOUSBHOST=1` capture steps in the card proceed as specified?

## Board

- Row move: none — #181 stays `HW`; the claim+capture card could not be attempted. A builder/dispatcher rebuild of `raw_capture` is needed before the next attempt.
