# PLAN A — UVC live capture, depth stream re-run (#raw_capture) — BLOCKED

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: e7e1ad200b4ff10a86008ec723bd53ff78503e62 (matches `main`)
- Target: build/tools/raw_capture/raw_capture (build-tree copy (fixed order 2: build/tools/<target>/<target>)); sha256 `b13a9c764c5bbff338e6d159a71c10bd7d90813c331f011a4cda941fca22345f`
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 21:10:52 >= newest source descriptor_probe.h 21:10:47
- Interposer: n/a — `raw_capture` links no vendor SDK; vendor-artifact/interposer rules do not apply
- Run start: 2026-10-21 22:36:08
- Peer: our raw_capture (UVC), vendor RPCServer stopped for this run

## Preconditions (vendor stop)

```
$ launchctl bootout gui/501/com.creality.RPCServer   # rc=0
$ pgrep -x RPCServer                                 # no output, rc=1 (empty, no admin kill needed)
```

## Command

```
build/tools/raw_capture/raw_capture --probe
build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 --sink tests/results/2026-10-21-plan-uvc-v2/depth.bin
```

(corrected flags per this re-run's work order: `--max-seconds`, file `--sink`; depth expectation corrected to 840-byte stride, 252,000-byte frame)

## Result

- Exit code: `--probe` = `0`; `--capture --stream depth` = `2`
- Verdict: **BLOCKED**
- Raw log: `2026-10-21-plan-uvc-depth-v2.raw.log` (same directory)
- Interposer confirmation: n/a (tool is exempt)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `--probe` reports depth mode geometry with stride 840 | **[PASS]** interface=1 endpoint=0x81 `fourcc=Y8 width=480 height=300 max_frame_bytes=252000 bytes_per_line=840 (derived)` — matches corrected expectation (840×300=252,000), not the earlier-stated 480×300=144,000 |
| 2 | corrected CLI flags (`--max-seconds`, file `--sink`) are accepted by the parser | **[PASS]** no `unknown argument` error this run |
| 3 | depth capture produces frames | **[FAIL]** `[capture] stop: source-error` / `[capture] detail: alternate setting 0 could not be selected: Unable to select alternate setting.` — 0 frames, 0 record bytes |
| 4 | sink file payload size vs corrected expectation (252,000 bytes) | **[FAIL]** `tests/results/2026-10-21-plan-uvc-v2/depth.bin` is 16 bytes on disk (header only, no frame payload) |

## Crash sweep

`scripts/hw-postflight.sh plan-uvc-depth-v2 '2026-10-21 22:36:08' raw_capture` →
`hw-postflight: no crash report for 'raw_capture' at/after 2026-10-21 22:36:08 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.`

## On failure or block — observable + one lead

- Observable: `build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 --sink tests/results/2026-10-21-plan-uvc-v2/depth.bin` → stdout: `[capture] sink: tests/results/2026-10-21-plan-uvc-v2/depth.bin (16 bytes)` / `[capture] stop: source-error` / `[capture] detail: alternate setting 0 could not be selected: Unable to select alternate setting.` / `[capture] frames: 0 (ir 0, depth 0, rgb 0), 0 record bytes`, exit `2`. The sink file on disk is 16 bytes (no frame payload), far short of the probe's reported `max_frame_bytes=252000` for depth. `--probe` itself ran clean (exit 0) and reported the depth mode on interface 1 / endpoint 0x81 with `bytes_per_line=840 (derived)`, confirming the corrected 252,000-byte expectation from the probe's own numbers.
- Lead: likely in the capture path's USB alternate-setting selection for the streaming interface (tools/raw_capture, around the UVC `SET_INTERFACE`/alt-setting call) — same symptom as the IR stream in this round, so the failure is not stream-specific.

## Open questions

- Is the alt-setting-selection failure specific to this run (e.g. a transient USB/host-controller state left over from stopping RPCServer), or does it reproduce on every `--capture` attempt now that the CLI flags are correct?
- Since both IR and depth streams hit the identical `source-error`/alt-setting failure, is this a single shared code path issue rather than two independent ones?

## Board

- Row move: none proposed by this record — reporting the observable only.
