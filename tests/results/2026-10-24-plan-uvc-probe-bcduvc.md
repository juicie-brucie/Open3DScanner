# UVC probe bcd_uvc / format_index / frame_index fields — PASS

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: 95218cc719b0145a952262a74de756406f8b10b7
- Target: build/tools/raw_capture/raw_capture (only copy; no vendor SDK, no interposer)
- Binary freshness:
  - build/tools/raw_capture/raw_capture  2026-10-24 00:45:07
  - tools/raw_capture/descriptor_probe.cpp  2026-10-24 00:44:56
  - tools/raw_capture/descriptor_probe.h  2026-10-24 00:44:22
  - Binary is newer than both sources — FRESH
- Interposer: not applicable (raw_capture links no vendor SDK)
- Run start: 2026-10-24 00:45:07

## Command

```
build/tools/raw_capture/raw_capture --probe
```

## Result

- Exit code: `0`
- Verdict: **PASS**
- Raw log: `2026-10-24-plan-uvc-probe-bcduvc.raw.log` (same directory)
- Interposer confirmation: not applicable

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Probe exits 0 | **[PASS]** exit code 0 |
| 2 | `bcd_uvc` field present in VideoControl output | **[PASS]** three interfaces each print `video_control bcd_uvc=0x0110 (probe_commit_control_length=34)` |
| 3 | `probe_commit_control_length` derived correctly (UVC 1.1 → 34 bytes) | **[PASS]** all three VC interfaces report 34 |
| 4 | `format_index` and `frame_index` present for depth stream (interface 1) | **[PASS]** `video_streaming format_index=3 frame_index=7` |
| 5 | `format_index` and `frame_index` present for IR stream (interface 3) | **[PASS]** `video_streaming format_index=2 frame_index=1` |
| 6 | `format_index` and `frame_index` present for RGB stream (interface 5) | **[PASS]** `video_streaming format_index=1 frame_index=2` |

## Extracted values

**VideoControl interfaces (all three):**
- `bcd_uvc` = 0x0110 (UVC 1.1)
- `probe_commit_control_length` = 34 bytes

**VideoStreaming interfaces:**

| Interface | Stream | format_index | frame_index | fourcc | resolution |
|-----------|--------|-------------|-------------|--------|------------|
| 1 | depth (stream[0]) | 3 | 7 | Y8   | 480×300 |
| 3 | IR (stream[1]) | 2 | 1 | Y8   | 960×600 |
| 5 | RGB (stream[2]) | 1 | 2 | YUY2 | 1280×800 |

## Crash sweep

no crash report for 'raw_capture' at/after 2026-10-24 00:45:00 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## Open questions

- The probe note states "the IR/depth assignment needs the RPC->endpoint map (OQ-169e)" — do the format_index/frame_index values printed for interfaces 1 and 3 correctly correspond to depth and IR respectively, or does the RPC map invert them?
- The depth stream (interface 1) reports `format_index=3 frame_index=7` but the IR stream (interface 3) reports `format_index=2 frame_index=1` — does the higher format_index on the depth interface reflect more descriptor entries for depth, and is that the expected device layout?
- All three VC interfaces report `bcd_uvc=0x0110` — is it architecturally expected that a single physical device exposes three independent VideoControl interfaces each with their own UVC version declaration?

## Board

- Row move: no board move — read-only data-gathering run as specified
