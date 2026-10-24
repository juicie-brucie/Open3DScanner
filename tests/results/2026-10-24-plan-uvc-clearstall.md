# PLAN A — UVC live capture clearStall retest, depth + IR streams (#raw_capture) — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: 95218cc719b0145a952262a74de756406f8b10b7
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 00:37:10 >= newest source iokit_source.mm 00:37:05
- Interposer: n/a — `raw_capture` links no vendor SDK; vendor-artifact/interposer rules do not apply
- Run start: 2026-10-24 00:38:00

## Preconditions (vendor stop)

```
$ launchctl bootout gui/501/com.creality.RPCServer   # rc=3 "Boot-out failed: 3: No such process" — was already stopped
$ pgrep -x RPCServer                                 # no output, rc=1 (empty)
```

## Commands

```
build/tools/raw_capture/raw_capture --probe

build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-clearstall/depth.bin

build/tools/raw_capture/raw_capture --capture --stream ir --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-clearstall/ir.bin
```

## Result

- Exit code: `--probe` = `0`; depth capture = `2`; IR capture = `2`
- Verdict: **FAIL**
- Raw log: `plan-uvc-clearstall.raw.log` (same directory)
- Interposer confirmation: n/a (tool is exempt)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `--probe` reports depth stream geometry (interface=1, endpoint=0x81, stride=840) | **[PASS]** `fourcc=Y8 width=480 height=300 max_frame_bytes=252000 bytes_per_line=840 (derived)` |
| 2 | `--probe` reports IR stream geometry (interface=3, endpoint=0x82, stride=960) | **[PASS]** `fourcc=Y8 width=960 height=600 max_frame_bytes=576000 bytes_per_line=960 (derived)` |
| 3 | Prior "Unable to send IO" error absent (clearStall fix confirmed) | **[FAIL]** error unchanged: `[capture] stop: source-error` / `[capture] detail: the endpoint read failed: Unable to send IO.` — both streams, same as prior run |
| 4 | Depth capture produces frames | **[FAIL]** 0 frames, 0 record bytes |
| 5 | Depth sink file payload size > 16 bytes (expect ≥252,000 bytes per frame) | **[FAIL]** `depth.bin` is 16 bytes on disk (header only) |
| 6 | IR capture produces frames | **[FAIL]** 0 frames, 0 record bytes |
| 7 | IR sink file payload size > 16 bytes (expect ≥576,000 bytes per frame) | **[FAIL]** `ir.bin` is 16 bytes on disk (header only) |

## Crash sweep

hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 00:38:00 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure — observable + one lead

- Observable: Both `--capture --stream depth` and `--capture --stream ir` exit `2`. The error is identical to the prior run [hw-contradicted: 2026-10-24-plan-uvc-retest] — `[capture] stop: source-error` / `[capture] detail: the endpoint read failed: Unable to send IO.` — 0 frames, both sink files 16 bytes (header only). The `clearStall` fix did not change the outcome.
- Lead: likely the `IOUSBHostPipe` async-read submission itself rejects the IO before reaching the endpoint — possibly the pipe is not in a state that accepts reads at the point `ReadPipeAsync` is called (`tools/raw_capture/iokit_source.mm`).

## Open questions

- If `clearStallWithError:` runs but the error is unchanged, does the error arise before or after the stall-clear call?
- Does the bulk endpoint require the host to first send a UVC `VS_COMMIT_CONTROL` SET_CUR request before it will accept reads?
- Is the pipe object returned by `copyPipeWithAddress:` in a valid/open state when the read is submitted, or has the interface open failed silently upstream?

## Board

- Row move: UVC capture rows (depth and IR) remain in `HW` — the `clearStall` fix did not change the `Unable to send IO` error; capture still fails on both streams.
