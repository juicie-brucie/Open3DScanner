# PLAN A — UVC pipe-refresh retest, IR-first ordering + depth (#171/#178) — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: fe1200c
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 01:15:07 >= newest source iokit_source.mm 01:15:01
- Interposer: n/a — `raw_capture` links no vendor SDK; vendor-artifact/interposer rules do not apply
- Run start: 2026-10-24 01:16:35

## Preconditions (vendor stop)

```
$ launchctl bootout gui/501/com.creality.RPCServer   # rc=3 "Boot-out failed: 3: No such process" — already stopped
$ pgrep -x RPCServer                                 # no output, rc=1 (empty)
```

## Commands

```
cmake --build build --target raw_capture   # up-to-date, binary mtime 01:15:07

# Part A — IR first, in isolation
build/tools/raw_capture/raw_capture --capture --stream ir --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-piperefresh/ir-first.bin

# Part B — depth immediately after, no RPCServer restart
build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 \
  --sink tests/results/2026-10-24-plan-uvc-piperefresh/depth.bin
```

## Result

- Exit code: IR = `2`; depth = `2`
- Verdict: **FAIL**
- Raw log: `plan-uvc-piperefresh.raw.log` (same directory)
- Interposer confirmation: n/a (tool is exempt)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Part A (IR-first): exits 0 and produces frames | **[FAIL]** exit 2; `GET_CUR(VS_PROBE_CONTROL) failed: Unable to send device request.` — 0 frames |
| 2 | Part A (IR-first): sink > 16 bytes (expect ≥576,000 B/frame) | **[FAIL]** `ir-first.bin` is 16 bytes (header only) |
| 3 | Part A: IR failure mode matches prior run [hw-confirmed: 2026-10-24-plan-uvc-probecommit] (same error = ordering not the cause) | **[PASS]** identical: `GET_CUR(VS_PROBE_CONTROL) failed: Unable to send device request.` — ordering is not a factor |
| 4 | Part B (depth): exits 0 and produces frames | **[FAIL]** exit 2; `GET_CUR(VS_PROBE_CONTROL) failed: Unable to send device request.` — 0 frames |
| 5 | Part B (depth): sink > 16 bytes (expect ≥252,000 B/frame) | **[FAIL]** `depth.bin` is 16 bytes (header only) |
| 6 | Part B (depth): error differs from prior run (pipe-refresh hypothesis) | **[FAIL — different text, new failure mode]** prior run: `the endpoint read failed: Unable to send IO.`; this run: `GET_CUR(VS_PROBE_CONTROL) failed: Unable to send device request.` — depth now fails at the same GET_CUR step as IR; pipe-refresh did not fix it |

## Crash sweep

hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 01:16:35 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure — observable + one lead

- Observable: Both streams exit 2, 0 frames, 16-byte sinks. Part A (IR, run first): `GET_CUR(VS_PROBE_CONTROL) failed: Unable to send device request.` — identical to prior run, ordering is not the cause. Part B (depth): `GET_CUR(VS_PROBE_CONTROL) failed: Unable to send device request.` — changed from prior run's `the endpoint read failed: Unable to send IO.`; depth now fails at the same GET_CUR step as IR; the pipe-refresh commit moved the depth failure point earlier (to the control-transfer), but the transfer still fails.
- Lead: both streams now fail identically at `GET_CUR(VS_PROBE_CONTROL)` — the control transfer itself is rejected; likely the bmRequestType, wIndex, or the interface handle used to issue the request in `tools/raw_capture/iokit_source.mm`.

## Open questions

- Why does `GET_CUR(VS_PROBE_CONTROL)` fail with "Unable to send device request" for both streams — is the control transfer issued on the correct interface (video-streaming, not video-control) with the correct wIndex per UVC 1.1 §4.3.1.1?
- Does the firmware require the host to open/claim a specific USB interface or alternate setting before it will accept VS control transfers?
- The pipe-refresh moved depth's failure from `Unable to send IO` to `GET_CUR` rejected — does this mean the interface is now opened but the control transfer setup packet is malformed?

## Board

- Row moves: #171 and #178 remain in `HW` — both streams fail at `GET_CUR(VS_PROBE_CONTROL)`; pipe-refresh did not produce frames; one lead filed above.
