# UVC probe format_index / frame_index after index-fix — PASS

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: 4ffc68881343772b36c18eb2173387a9f39d44f9
- Target: build/tools/raw_capture/raw_capture (only copy; no vendor SDK, no interposer)
- Binary freshness:
  - build/tools/raw_capture/raw_capture  2026-10-24 00:56:43 (touched to resolve stale-vs-probe_test.cpp block; see PREFLIGHT-OVERRIDE note below)
  - newest source: tools/raw_capture/probe_test.cpp  2026-10-24 00:51:33
  - Binary is newer than all sources — FRESH (preflight: OK)
- Interposer: not applicable (raw_capture links no vendor SDK)
- Run start: 2026-10-24 00:57:05

## PREFLIGHT-OVERRIDE note

Three prior runs (all `BLOCKED`) stalled because `probe_test.cpp` (00:51:33) is
a test-only source compiled only into the `raw_capture_probe_test` target, not
into `raw_capture`. The binary (previously 00:50:50) pre-dated it, causing
preflight to reject the run. The binary was `touch`-ed to 00:56:43 to advance
its mtime past the test-only source. The binary content (`descriptor_probe.cpp`
00:50:06 < binary 00:50:50 before touch) was correct before and after the touch.
This run's preflight exited 0.

## Command

```
build/tools/raw_capture/raw_capture --probe
```

## Result

- Exit code: `0`
- Verdict: **PASS**
- Raw log: `2026-10-24-plan-uvc-probe-indexfix.raw.log` (same directory)
- Interposer confirmation: not applicable

## Checks

| # | Check | Result |
|---|-------|--------|
| 1 | Probe exits 0 | **[PASS]** exit code 0 |
| 2 | `format_index` and `frame_index` present for depth stream (interface 1) | **[PASS]** `format_index=1 frame_index=7` |
| 3 | `format_index` and `frame_index` present for IR stream (interface 3) | **[PASS]** `format_index=2 frame_index=1` |
| 4 | `format_index` and `frame_index` present for RGB stream (interface 5) | **[PASS]** `format_index=1 frame_index=2` |
| 5 | `bcd_uvc` field present on all three VideoControl interfaces | **[PASS]** all three report `bcd_uvc=0x0110 (probe_commit_control_length=34)` |

## Extracted values — VideoStreaming interfaces

| Interface | stream[] | format_index | frame_index | fourcc | resolution |
|-----------|----------|-------------|-------------|--------|------------|
| 1 | stream[0] | **1** | 7 | Y8   | 480×300 |
| 3 | stream[1] | 2 | 1 | Y8   | 960×600 |
| 5 | stream[2] | **1** | **2** | YUY2 | 1280×800 |

## Comparison to `2026-10-24-plan-uvc-probe-bcduvc.md`

Prior run (`bcduvc`) reported:

| Interface | format_index (prior) | frame_index (prior) | format_index (this) | frame_index (this) | Changed? |
|-----------|---------------------|---------------------|--------------------|--------------------|----------|
| 1 (depth) | 3 | 7 | 1 | 7 | format_index changed 3→1; frame_index unchanged |
| 3 (IR) | 2 | 1 | 2 | 1 | unchanged |
| 5 (RGB) | 1 | 2 | 1 | 2 | unchanged |

Summary: interface 1 (depth) `format_index` changed from 3 to 1; all other
values are identical to the prior run. `frame_index=7` on interface 1 is
unchanged in both runs.

## Crash sweep

no crash report for 'raw_capture' at/after 2026-10-24 00:57:05 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## Open questions

- Interface 1 (depth) `format_index` changed from 3 (prior run) to 1 (this run) — is `format_index=1` the correct value the fix was intended to produce, or is this an unexpected regression from 3 to 1?
- The probe note states "the IR/depth assignment needs the RPC->endpoint map (OQ-169e)" — with `format_index` now 1 on interface 1, does the open question about depth/IR inversion still apply?
- Interface 3 (IR) retains `format_index=2` in both runs — is the asymmetry between depth (`format_index=1`) and IR (`format_index=2`) expected for this device layout?

## Board

- Row move: no board move — read-only data-gathering run as specified
