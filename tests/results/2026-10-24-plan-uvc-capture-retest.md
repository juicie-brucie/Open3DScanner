# PLAN A — UVC live capture retest, depth + IR streams (#raw_capture) — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture (build-tree copy (fixed order 2: build/tools/<target>/<target>)); binary sha-256 62b96301f8852694af7e54d48ccab54113eaf311a0d3b41eb025fe1e04ff5af8
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 00:30:34 >= newest source iokit_source.mm 00:30:26
- Interposer: n/a — `raw_capture` links no vendor SDK; vendor-artifact/interposer rules do not apply
- Run start: 2026-10-24 00:31:52
- Peer: our raw_capture (UVC), vendor RPCServer stopped for this run
- Raw logs: `depth.raw.log`, `ir.raw.log`, `probe.raw.log` (in the record's own directory)
- Fix under retest: `tools/raw_capture/iokit_source.mm`, `UsbInterfaceHandle::Open` — skip `selectAlternateSetting:` when the interface is already at the target `bAlternateSetting` (built via `cmake --build build --target raw_capture`; device-free `raw_capture_test` 81/81 passes but does not exercise this IOKit path)

## Preconditions (vendor stop)

```
$ launchctl bootout gui/501/com.creality.RPCServer   # rc=0
$ pgrep -x RPCServer                                 # no output, rc=1
```

## Command

```
build/tools/raw_capture/raw_capture --probe
build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 --sink tests/results/2026-10-24-plan-uvc-capture-retest/depth.bin
build/tools/raw_capture/raw_capture --capture --stream ir    --max-seconds 5 --sink tests/results/2026-10-24-plan-uvc-capture-retest/ir.bin
```

## Result

- Exit code: `--probe` = `0`; `--capture --stream depth` = `2`; `--capture --stream ir` = `2`
- Verdict: **FAIL**
- Raw log: `2026-10-24-plan-uvc-capture-retest/probe.raw.log`, `2026-10-24-plan-uvc-capture-retest/depth.raw.log`, `2026-10-24-plan-uvc-capture-retest/ir.raw.log` (subdirectory alongside this record)
- Interposer confirmation: n/a (tool is exempt)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `--probe` reports depth mode geometry with stride 840 | **[PASS]** interface=1 endpoint=0x81 `fourcc=Y8 width=480 height=300 max_frame_bytes=252000 bytes_per_line=840 (derived)` — matches prior probe exactly |
| 2 | `--probe` reports IR mode geometry with stride 960 | **[PASS]** interface=3 endpoint=0x82 `fourcc=Y8 width=960 height=600 max_frame_bytes=576000 bytes_per_line=960 (derived)` — matches prior probe exactly |
| 3 | depth capture no longer hits the alt-setting-0 error the fix targeted | **[PASS]** no `alternate setting 0 could not be selected` text this run |
| 4 | depth capture produces frames | **[FAIL]** `[capture] stop: source-error` / `[capture] detail: the endpoint read failed: Unable to send IO.` — 0 frames, 0 record bytes |
| 5 | depth sink file payload size vs expectation (252,000 bytes) | **[FAIL]** `tests/results/2026-10-24-plan-uvc-capture-retest/depth.bin` is 16 bytes on disk (header only, no frame payload) |
| 6 | IR capture no longer hits the alt-setting-0 error the fix targeted | **[PASS]** no `alternate setting 0 could not be selected` text this run |
| 7 | IR capture produces frames | **[FAIL]** `[capture] stop: source-error` / `[capture] detail: the endpoint read failed: Unable to send IO.` — 0 frames, 0 record bytes (identical text to depth) |
| 8 | IR sink file payload size vs expectation (576,000 bytes) | **[FAIL]** `tests/results/2026-10-24-plan-uvc-capture-retest/ir.bin` is 16 bytes on disk (header only, no frame payload) |

## Crash sweep

`scripts/hw-postflight.sh plan-uvc-capture-retest '2026-10-24 00:31:52' raw_capture` →
`hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 00:31:52 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.`

## On failure or block — observable + one lead

- Observable: `build/tools/raw_capture/raw_capture --capture --stream depth --max-seconds 5 --sink tests/results/2026-10-24-plan-uvc-capture-retest/depth.bin` → stdout: `[capture] sink: tests/results/2026-10-24-plan-uvc-capture-retest/depth.bin (16 bytes)` / `[capture] stop: source-error` / `[capture] detail: the endpoint read failed: Unable to send IO.` / `[capture] frames: 0 (ir 0, depth 0, rgb 0), 0 record bytes`, exit `2`. The IR run under the same conditions produces byte-for-byte the identical stdout text (substituting the IR sink path), also exit `2`, also a 16-byte sink file. This is a different error string than the prior round's `alternate setting 0 could not be selected: Unable to select alternate setting.` — that text does not appear in either run this time, so the alt-setting-0 fix changed the failure mode but capture still does not produce frames on either stream.
- Lead: likely in the capture path's bulk-endpoint read/IO submission for the streaming interface (tools/raw_capture/iokit_source.mm, the read call that follows `UsbInterfaceHandle::Open` — pipe/endpoint selection or the actual `ReadPipe`/async-IO call for the bulk endpoint) — same symptom on both streams, so still not stream-specific.

## Open questions

- Is "Unable to send IO" a new IOKit-layer failure exposed now that the alt-setting call is skipped (e.g. the bulk pipe reference is never obtained/opened when `selectAlternateSetting:` is bypassed), or an unrelated, pre-existing second failure that the alt-setting error was previously masking?
- Since both IR and depth hit the identical new `source-error`/"Unable to send IO" text, is this again a single shared code path issue rather than two independent ones?

## Board

- Row move: none proposed by this record — reporting the observable only.
