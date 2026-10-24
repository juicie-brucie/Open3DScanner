# PLAN A — UVC VS_PROBE_CONTROL/VS_COMMIT_CONTROL negotiation retest, depth + IR (#171/#178) — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: 3a9dcf0
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 01:07:05 >= newest source iokit_source.mm 01:06:55
- Interposer: n/a — `raw_capture` links no vendor SDK; vendor-artifact/interposer rules do not apply
- Run start: 2026-10-24 01:09:41

## Preconditions (vendor stop)

```
$ launchctl bootout gui/501/com.creality.RPCServer   # rc=3 "Boot-out failed: 3: No such process" — was already stopped
$ pgrep -x RPCServer                                 # no output, rc=1 (empty)
```

## Commands

```
cmake --build build --target raw_capture   # already up to date

build/tools/raw_capture/raw_capture --probe

build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-probecommit/depth.bin

build/tools/raw_capture/raw_capture --capture --stream ir --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-probecommit/ir.bin
```

## Result

- Exit code: `--probe` = `0`; depth capture = `2`; IR capture = `2`
- Verdict: **FAIL**
- Raw log: `plan-uvc-probecommit.raw.log` (same directory)
- Interposer confirmation: n/a (tool is exempt)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `--probe` reports depth stream geometry (interface=1, endpoint=0x81, stride=840) | **[PASS]** `fourcc=Y8 width=480 height=300 max_frame_bytes=252000 bytes_per_line=840 (derived)` |
| 2 | `--probe` reports IR stream geometry (interface=3, endpoint=0x82, stride=960) | **[PASS]** `fourcc=Y8 width=960 height=600 max_frame_bytes=576000 bytes_per_line=960 (derived)` |
| 3 | Depth capture exits 0 and produces frames | **[FAIL]** exit 2; `[capture] stop: source-error` / `[capture] detail: the endpoint read failed: Unable to send IO.` — 0 frames |
| 4 | Depth error differs from clearstall run (new negotiation error text) | **[FAIL]** identical to [hw-contradicted: 2026-10-24-plan-uvc-clearstall]: `the endpoint read failed: Unable to send IO.` — negotiation step not named |
| 5 | Depth sink file payload size > 16 bytes (expect ≥252,000 bytes/frame) | **[FAIL]** `depth.bin` is 16 bytes on disk (header only) |
| 6 | IR capture exits 0 and produces frames | **[FAIL]** exit 2; `[capture] stop: source-error` / `[capture] detail: GET_CUR(VS_PROBE_CONTROL) failed: Unable to send device request.` — 0 frames |
| 7 | IR error differs from clearstall run | **[PASS — partial]** new error text: `GET_CUR(VS_PROBE_CONTROL) failed: Unable to send device request.` — negotiation code ran and reached the GET_CUR step, which itself failed |
| 8 | IR sink file payload size > 16 bytes (expect ≥576,000 bytes/frame) | **[FAIL]** `ir.bin` is 16 bytes on disk (header only) |

## Crash sweep

hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 01:09:41 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure — observable + one lead

- Observable: Both captures exit 2, 0 frames, 16-byte sinks. Depth stream: `[capture] detail: the endpoint read failed: Unable to send IO.` — identical to [hw-contradicted: 2026-10-24-plan-uvc-clearstall], negotiation code did not reach or alter this path. IR stream: `[capture] detail: GET_CUR(VS_PROBE_CONTROL) failed: Unable to send device request.` — new error; the negotiation code ran, issued the GET_CUR control transfer, and the OS rejected it with "Unable to send device request."
- Lead: the `GET_CUR(VS_PROBE_CONTROL)` control-transfer is failing — likely the IOUSBHostDevice/interface used for the request, or the bmRequestType/wIndex fields in the setup packet, in the UVC probe sequence added to `tools/raw_capture/iokit_source.mm`.

## Open questions

- Why does the depth stream still show `Unable to send IO` (the pre-negotiation error) rather than the new `GET_CUR(VS_PROBE_CONTROL)` error — does the depth path skip the negotiation, or hit a different failure before it?
- `GET_CUR(VS_PROBE_CONTROL)` fails with "Unable to send device request" — is the control transfer directed at the correct interface number (video-streaming, not video-control), and is `wIndex` set to the streaming interface number as required by UVC 1.1 §4.3.1.1?
- Does the firmware require the host to open a specific USB configuration or claim the interface before accepting VS control transfers?

## Board

- Row moves: #171 and #178 remain in `HW` — both streams fail; IR shows the negotiation code reached `GET_CUR(VS_PROBE_CONTROL)` but the control transfer is rejected; depth path is unchanged from the prior run.
