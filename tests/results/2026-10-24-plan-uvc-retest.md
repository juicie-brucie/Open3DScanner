# PLAN A — UVC live capture retest, depth + IR streams (#raw_capture) — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: 95218cc719b0145a952262a74de756406f8b10b7
- Target: build/tools/raw_capture/raw_capture (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 00:30:34 >= newest source iokit_source.mm 00:30:26
- Interposer: n/a — `raw_capture` links no vendor SDK; vendor-artifact/interposer rules do not apply
- Run start: 2026-10-24 00:33:25
- Peer: our raw_capture (UVC), vendor RPCServer was not running (bootout returned "No such process"; pgrep -x RPCServer empty, rc=1)

## Preconditions (vendor stop)

```
$ launchctl bootout gui/501/com.creality.RPCServer   # rc=3 "No such process" — was already stopped
$ pgrep -x RPCServer                                 # no output, rc=1 (empty)
```

## Commands

```
build/tools/raw_capture/raw_capture --probe

build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-retest/depth.bin

build/tools/raw_capture/raw_capture --capture --stream ir --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-retest/ir.bin
```

## Result

- Exit code: `--probe` = `0`; depth capture = `2`; IR capture = `2`
- Verdict: **FAIL**
- Raw log: `plan-uvc-retest.raw.log` (same directory)
- Interposer confirmation: n/a (tool is exempt)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `--probe` reports depth stream geometry (interface=1, endpoint=0x81, stride=840) | **[PASS]** `fourcc=Y8 width=480 height=300 max_frame_bytes=252000 bytes_per_line=840 (derived)` |
| 2 | `--probe` reports IR stream geometry (interface=3, endpoint=0x82, stride=960) | **[PASS]** `fourcc=Y8 width=960 height=600 max_frame_bytes=576000 bytes_per_line=960 (derived)` |
| 3 | Prior alt-setting error absent (fix confirmed) | **[PASS]** neither run produced `alternate setting 0 could not be selected: Unable to select alternate setting.` — error text has changed entirely |
| 4 | Depth capture produces frames | **[FAIL]** `[capture] stop: source-error` / `[capture] detail: the endpoint read failed: Unable to send IO.` — 0 frames, 0 record bytes |
| 5 | Depth sink file payload size > 16 bytes (expect ≥252,000 bytes per frame) | **[FAIL]** `depth.bin` is 16 bytes on disk (header only) |
| 6 | IR capture produces frames | **[FAIL]** `[capture] stop: source-error` / `[capture] detail: the endpoint read failed: Unable to send IO.` — 0 frames, 0 record bytes |
| 7 | IR sink file payload size > 16 bytes (expect ≥576,000 bytes per frame) | **[FAIL]** `ir.bin` is 16 bytes on disk (header only) |

## Crash sweep

`scripts/hw-postflight.sh plan-uvc-retest '2026-10-24 00:33:25' raw_capture` →
`hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 00:33:25 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.`

## On failure — observable + one lead

- Observable: Both `--capture --stream depth` and `--capture --stream ir` exit `2`. The prior error (`alternate setting 0 could not be selected: Unable to select alternate setting.`) is **absent** — the alt-setting fix took effect. The new error for both streams is: `[capture] stop: source-error` / `[capture] detail: the endpoint read failed: Unable to send IO.` Both sink files are 16 bytes (header only, 0 frames).
- Lead: likely in the bulk-endpoint read/async-IO setup after the interface is opened, where the IOKit USB read is submitted to the endpoint (`tools/raw_capture/iokit_source.mm`, around the `ReadPipeAsync`/`ReadPipe` call).

## Open questions

- The alt-setting error is resolved; the new error is "Unable to send IO" on the endpoint read itself — does the capture path open the interface's pipe before attempting the read, or does it go straight to the read?
- Both streams fail identically with the same new error — is the bulk-read setup shared between depth and IR in a common code path?
- Does the device require a UVC `VS_COMMIT_CONTROL` negotiation before the host can read from the bulk endpoint, and is that step present in the current capture path?

## Board

- Row move: rows for depth and IR UVC capture remain in `HW` — the alt-setting fix is confirmed effective but a new source-error (`Unable to send IO`) blocks capture on both streams.
