# PLAN A — UVC live capture, IR stream (#raw_capture) — BLOCKED

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 21:10:52 >= newest source descriptor_probe.h 21:10:47
- Interposer: n/a — `raw_capture` links no vendor SDK; vendor-artifact/interposer rules do not apply
- Run start: 2026-10-21 22:22:15
- Peer: our raw_capture (UVC), vendor RPCServer stopped for this run

## Preconditions (vendor stop)

```
$ launchctl bootout gui/501/com.creality.RPCServer   # rc=0
$ pgrep -x RPCServer                                 # no output, rc=1 (empty, no admin kill needed)
```

## Command

```
build/tools/raw_capture/raw_capture --capture --stream ir --seconds 5 --sink <outdir>
```

(exact command from the work order)

## Result

- Exit code: `1`
- Verdict: **BLOCKED**
- Raw log: `2026-10-21-plan-uvc-ir.raw.log` (same directory)
- Interposer confirmation: n/a (tool is exempt)

## On failure or block — observable + one lead

- Observable: `build/tools/raw_capture/raw_capture --capture --stream ir --seconds 5 --sink <outdir>` → stdout/stderr: `unknown argument: --seconds`, exit `1`. The tool's own `--help` shows the capture form as `raw_capture --capture [--mock] --sink <file> [--stream ir|depth|rgb|both] [--max-frames N] [--max-bytes N] [--max-seconds N]` — `--sink` takes a file, not a directory, and the duration flag is `--max-seconds`, not `--seconds`. No capture ran; `--probe` (run separately, see `2026-10-21-plan-uvc-probe.raw.log`) succeeded and reports the IR mode as `width=960 height=600 max_frame_bytes=576000` on interface 3 / endpoint 0x82.
- Lead: likely a CLI-contract mismatch between the work order's assumed `raw_capture` invocation and the tool's actual argument parser (tools/raw_capture/main.cpp).

## Crash sweep

`scripts/hw-postflight.sh plan-uvc-ir '2026-10-21 22:22:15' raw_capture` →
`hw-postflight: no crash report for 'raw_capture' at/after 2026-10-21 22:22:15 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.`

## Open questions

- Does the work order's `--seconds`/`--sink <dir>` form belong to a different build of `raw_capture`, or was the CLI contract changed after the order was written?
- With the corrected flags (`--max-seconds`, `--sink <file>`), would the IR capture reach 576,000-byte payloads as the probe's `max_frame_bytes` reports?

## Board

- Row move: none proposed by this record — reporting the observable only.
