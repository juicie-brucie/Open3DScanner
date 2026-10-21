# PLAN #171 / #178 — raw_capture live UVC/HID capture — BLOCKED

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 16:02:15 >= newest source descriptor_probe.cpp 16:02:09
- Interposer: n/a — the target does not create an OBScan context; no launcher wrapping expected
- Run start: 2026-10-21 16:08:59

## Command

```
build/tools/raw_capture/raw_capture --capture --sink /tmp/test_capture.bin --stream ir --max-frames 5
```

(preceded by `--table` to confirm descriptor_table.h state; combined raw log: 2026-10-21-plan171-178-capture-blocked.raw.log)

## Result

- Exit code: `2`
- Verdict: **BLOCKED**
- Raw log: `2026-10-21-plan171-178-capture-blocked.raw.log` (same directory)
- Interposer confirmation: n/a

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | descriptor_table.h IR USB endpoint known | **[FAIL]** `[table] stream ir rpc port 10013 (known, suffix 0-2-12)` / `usb endpoint: unknown` |
| 2 | descriptor_table.h depth USB endpoint known | **[FAIL]** `[table] stream depth rpc port 10010 (known, suffix 0-2-10)` / `usb endpoint: unknown` |
| 3 | live capture starts | **[FAIL]** `[capture] stop: not-configured` / `[capture] frames: 0 (ir 0, depth 0), 0 record bytes` |

## Crash sweep

no crash report for 'raw_capture' at/after 2026-10-21 16:08:59 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure or block — observable + one lead

- Observable: `build/tools/raw_capture/raw_capture --capture --sink /tmp/test_capture.bin --stream ir --max-frames 5` exits 2 with: `[capture] stop: not-configured` / `[capture] detail: not-configured: ir interface_number, alternate_setting, endpoint_address, transfer, pixel_format, bytes_per_line, width/height (run raw_capture --probe and fill descriptor_table.h)` / `[capture] frames: 0 (ir 0, depth 0), 0 record bytes`
- Lead: descriptor_table.h has IR and depth USB endpoint assignments as "unknown" — requires the OQ-169e RPC→endpoint map to be resolved and filled into tools/raw_capture/descriptor_table.h before the live capture gate can run

## Open questions

- Which of the three UVC VS endpoints (0x81 Y8 480×300, 0x82 Y8 960×600, 0x83 YUY2 1280×800) corresponds to IR and which to depth? (OQ-169e)
- Can the RPC→endpoint assignment be deduced from the RPC port suffix patterns (IR=0-2-12, depth=0-2-10) or does it require a vendor trace?

## Board

- Row move: #171 stays `HW` — live capture gate blocked pending descriptor_table.h fill (OQ-169e)
- Row move: #178 probe gate is DONE (see 2026-10-21-plan178-probe); live-capture gate stays open
