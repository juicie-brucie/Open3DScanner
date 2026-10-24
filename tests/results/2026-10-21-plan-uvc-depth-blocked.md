# PLAN A — UVC live capture, depth stream (#raw_capture) — BLOCKED

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 21:10:52 >= newest source descriptor_probe.h 21:10:47
- Interposer: n/a — `raw_capture` links no vendor SDK; vendor-artifact/interposer rules do not apply
- Run start: 2026-10-21 22:22:15 (same session as the IR attempt; vendor already stopped — see `2026-10-21-plan-uvc-ir-blocked.md` for the stop/pgrep output)
- Peer: our raw_capture (UVC), vendor RPCServer stopped for this run

## Command

```
build/tools/raw_capture/raw_capture --capture --stream depth --seconds 5 --sink <outdir>
```

(exact command from the work order)

## Result

- Exit code: `1`
- Verdict: **BLOCKED**
- Raw log: `2026-10-21-plan-uvc-depth.raw.log` (same directory)
- Interposer confirmation: n/a (tool is exempt)

## On failure or block — observable + one lead

- Observable: `build/tools/raw_capture/raw_capture --capture --stream depth --seconds 5 --sink <outdir>` → stdout/stderr: `unknown argument: --seconds`, exit `1`. Same CLI-contract mismatch as the IR attempt (`2026-10-21-plan-uvc-ir-blocked.md`): the tool's `--help` requires `--sink <file>` and `--max-seconds N`, not `--seconds <dir-flag>`. No capture ran. `--probe` separately reports the depth mode (interface 1 / endpoint 0x81) as `width=480 height=300 max_frame_bytes=252000`.
- Lead: likely a CLI-contract mismatch between the work order's assumed `raw_capture` invocation and the tool's actual argument parser (tools/raw_capture/main.cpp).

## Crash sweep

`scripts/hw-postflight.sh plan-uvc-depth '2026-10-21 22:22:15' raw_capture` →
`hw-postflight: no crash report for 'raw_capture' at/after 2026-10-21 22:22:15 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.`

## Open questions

- Does the plan's documented 144,000-byte depth payload figure reconcile with the probe's `max_frame_bytes=252000` for this interface, once a capture actually runs with the corrected flags?

## Board

- Row move: none proposed by this record — reporting the observable only.
