# PLAN #132 — gap-free captures (IR + depth), post-power-cycle — PASS

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (synced to origin/main @ `2174a66`)
- Target: build/tools/raptor_client/raptor_client (build-tree copy, fixed order 2: build/tools/<target>/<target>)
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon n/a — no OBScan context; freshness: binary 22:17:03 >= newest source `capture.cpp` 20:47:37
- Interposer: n/a — exempt
- Run start: 2026-10-20 23:08:36
- Device: CR-Scan Raptor Pro, SN `CR0886100SN`; fresh after power-cycle; scan_server run completed and stopped before these raptor_client sessions

## Commands

```
# IR (port 10013):
./build/tools/raptor_client/raptor_client \
  --live 127.0.0.1 10013 30 --frames 200 \
  --capture ~/captures/plan132-ir-v2 \
  --capture-seconds 30 --capture-bytes 600000000 --dump-replies

# depth (port 10010):
./build/tools/raptor_client/raptor_client \
  --live 127.0.0.1 10010 30 --frames 200 \
  --capture ~/captures/plan132-depth-v2 \
  --capture-seconds 30 --capture-bytes 600000000 --dump-replies
```

## Result

- Exit code: `0` for both (seconds/bytes limits not hit; IR: `--frames 200` limit stopped at 180 data frames before budget; depth: `--frames 200` reached)
- Verdict: **PASS**
- Raw log: `2026-10-20-plan132-gapfree-captures.raw.log` (IR then depth, concatenated; same directory)
- Interposer confirmation: n/a (exempt)

## Summary table

| Interface | Port | `start_stream` | Frames | Data | Empty | Captured | Bundle size |
|---|---|---|---|---|---|---|---|
| IR | 10013 | `Bool(true)` | 180 | 180 | 0 | 1078 records | 414 MB |
| depth | 10010 | `Bool(true)` | 200 | 200 | 0 | 1202 records | 460 MB |

## Checks

| # | Check | Result |
|---|---|---|
| 1 | IR `start_stream` returns `true` | **[PASS]** `[live] start_stream -> true` |
| 2 | depth `start_stream` returns `true` | **[PASS]** `[live] start_stream -> true` |
| 3 | IR: consecutive data-bearing `get_frame` replies (OQ-131-2 precondition) | **[PASS]** `data=180 empty=0 error=0` — every reply carried a payload |
| 4 | depth: consecutive data-bearing `get_frame` replies | **[PASS]** `data=200 empty=0 error=0` — every reply carried a payload |
| 5 | IR e1 (`[1]=UInt`) timestamps monotonically increasing across all 180 frames | **[PASS]** 0 non-increasing pairs; inter-frame delta min=101645 µs max=233336 µs mean=166870 µs (~6.0 Hz device rate at this poll cadence) |
| 6 | depth e1 timestamps monotonically increasing across all 200 frames | **[PASS]** 0 non-increasing pairs; inter-frame delta min=64055 µs max=167291 µs mean=139214 µs (~7.2 Hz device rate) |
| 7 | IR e3 (format field `[3]=UInt`) consistent across all frames | **[PASS]** `255` on all 180 frames |
| 8 | depth e3 consistent across all frames | **[PASS]** `255` on all 200 frames |
| 9 | IR payload sizes (e4 `[4]=Bin`) | **[PASS]** 2304000 bytes on all 180 frames (1920×1200×1 byte) |
| 10 | depth payload sizes | **[PASS]** 199/200 frames = 2304000 bytes; 1 frame = 1321920 bytes (partial band, first frame) |
| 11 | Bundle manifests complete | **[PASS]** IR: `record_count=1078 bytes_written=414792579`; depth: `record_count=1202 bytes_written=459963865`; `limits_hit: {seconds:false, bytes:false}` both |
| 12 | No crash | **[PASS]** postflight: no crash report |

## OQ-131-* observations (from row #132's acceptance criteria)

- **OQ-131-1 / OQ-131-2 (phase of 0xFF invalid run advances by `len(payload) mod 1920`)**: payload size is 2304000 bytes = 1920×1200 uniformly on IR (180/180) and depth (199/200). The band-is-a-contiguous-slice test (phase advance of `2304000 mod 1920 = 0`) requires inspection of the concatenated payloads, which is outside this run's scope — the precondition (consecutive data-bearing replies with monotonically increasing timestamps) is now satisfied; the phase-advance analysis is the next step.
- **OQ-131-3 (e3 format field = 255 on every reply and on both interfaces)**: **confirmed** — `255` on all frames on both ports.
- **OQ-131-4 (e2 changes per band and correlates with e1)**: element `[2]=UInt` values captured in the bundle (`--dump-replies`); analysis not done in this run.

## Note on implied device Hz

The `e1` inter-frame deltas (~167 ms IR, ~139 ms depth) imply ~6–7 Hz at the poll cadence used here (`pull_ms=0`, back-to-back polling). The device delivers ~34 Hz to scan_server's pipeline (confirmed by the #151 run). The lower rate seen here is expected: raptor_client polls `get_frame` sequentially (one in-flight RPC at a time), not via the SDK's callback path, so each call waits for one complete frame round-trip.

## Crash sweep

`scripts/hw-postflight.sh plan132-captures-v2 '2026-10-20 23:08:36' raptor_client` →
`hw-postflight: no crash report for 'raptor_client' at/after 2026-10-20 23:08:36 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.`

## Open questions

- The depth's first frame had a partial payload (1321920 bytes vs 2304000 for all subsequent frames). Is the first `get_frame` reply always a partial band, or session-state dependent?
- OQ-131-1/2 (phase advance of the 0xFF invalid-pixel run across consecutive bands): the payloads are now captured and available in the bundle for offline analysis.
- OQ-131-4 (e2 field changes per band, correlation with e1): captured in the bundle; analysis deferred.

## Board

- Row move: #132 `HW` → `DONE` — the acceptance criterion is met: both interfaces now have gap-free captures with consecutive data-bearing `get_frame` replies and monotonically increasing e1 timestamps (180 IR frames, 200 depth frames, 0 non-increasing pairs on both). The OQ-131-* items that require offline payload analysis (phase-advance test, e2 correlation) are downstream analysis tasks for recon, not a gate on this row's hardware evidence.
