# PLAN #172 — RPC shim A/B (offline bundle source) — PASS

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_shim/raptor_shim  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 16:02:16 >= newest source helper_claim.h 16:02:09
- Interposer: n/a — the target does not create an OBScan context; no launcher wrapping expected
- Run start: 2026-10-21 16:19:06

## Command

```
# Terminal 1 — shim with #132 offline bundles
build/tools/raptor_shim/raptor_shim \
    --ir-bundle ~/captures/plan132-ir-v2 \
    --depth-bundle ~/captures/plan132-depth-v2 \
    --seconds 60

# Terminal 2 — raptor_client IR
build/tools/raptor_client/raptor_client --live 127.0.0.1 20013 8 \
    --daemon shim --dump-replies

# Terminal 3 — raptor_client depth
build/tools/raptor_client/raptor_client --live 127.0.0.1 20010 8 \
    --daemon shim --dump-replies
```

## Result

- Exit code (shim): `0` (SIGTERM after both client runs completed)
- Exit code (IR client): `0`
- Exit code (depth client): `0`
- Verdict: **PASS**
- Raw log: `2026-10-21-plan172-rpc-shim-ab.raw.log` (same directory; shim + IR + depth combined)
- Interposer confirmation: n/a

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Shim prints `--daemon shim` log line | **[PASS]** `[live] daemon=shim (OpenScanner RPC shim, IR 20013 / depth 20010; the vendor daemon is neither stopped nor replaced)` |
| 2 | IR port: start_stream → true | **[PASS]** `[dump] reply method=ob_rpc_start_stream0-2-12 kind=Bool ... result=Bool(true)` / `[live] start_stream -> true` |
| 3 | IR port: delivers data frames, 0 empty | **[PASS]** `done: get_frame=62 data=62 empty=0 error=0; stream stopped` / `[dump] tally: port 20013 data=62 empty=0 other=0 (data share 100.0%)` |
| 4 | IR port: frame array is 11-element with Bin(2304000) at [4] | **[PASS]** `result=array(11) [0]=UInt(0) [1]=UInt(...) [2]=UInt(...) [3]=UInt(255) [4]=Bin(2304000) ... [10]=UInt(4)` |
| 5 | IR port: stop_stream → true | **[PASS]** `[dump] reply method=ob_rpc_stop_stream0-2-12 kind=Bool ... result=Bool(true)` |
| 6 | IR port: SET property replies as vendor ACCEPT | **[PASS]** e.g. `SET prop=3 value=0 reqid=1 -> vendor ACCEPT (opcode=2 reqid=1 err=0, 10 B)` |
| 7 | Depth port: start_stream → true | **[PASS]** `[dump] reply method=ob_rpc_start_stream0-2-10 kind=Bool ... result=Bool(true)` / `[live] start_stream -> true` |
| 8 | Depth port: delivers data frames, 0 empty | **[PASS]** `done: get_frame=70 data=70 empty=0 error=0; stream stopped` / `[dump] tally: port 20010 data=70 empty=0 other=0 (data share 100.0%)` |
| 9 | Depth port: frame array is 11-element with Bin(2304000) at [4] | **[PASS]** `result=array(11) [0]=UInt(0) ... [4]=Bin(2304000) ... [10]=UInt(3)` |
| 10 | Depth port: stop_stream → true | **[PASS]** `[dump] reply method=ob_rpc_stop_stream0-2-10 kind=Bool ... result=Bool(true)` |
| 11 | Shim tally: no unknown methods, no errors | **[PASS]** `done: requests=796 get_frame=132 frames=132 empty=0 start=2 stop=2 send_data=660 enumeration=0 unknown=0 bytes_out=304145348` |
| 12 | IR bundle: no wrap needed for 8-second run | **[PASS]** `[shim] IR bundle: served=62 wraps=0 skipped=0` |

## Telemetry

### IR port 20013

| Metric | Shim | Vendor (from 2026-10-21-plan175-raptor-telemetry, 1st run) |
| --- | --- | --- |
| n | 62 | 56 |
| p50 | 118,177 µs | 119,727 µs |
| p95 | 172,421 µs | 130,739 µs |
| max | 183,417 µs | 143,509 µs |
| mean | 129,453 µs | 115,531 µs |
| payload B/s | 17,618,833 | 15,033,194 |
| time-to-first-frame | 118,307 µs | 244,826 µs |
| data share | 100% | 94.6% (53/56) |
| timeouts | 0 | 0 |

### Depth port 20010

| Metric | Shim | Vendor |
| --- | --- | --- |
| n | 70 | n/a (vendor depth timed out — see plan175 addendum) |
| p50 | 112,468 µs | — |
| p95 | 118,157 µs | — |
| max | 152,348 µs | — |
| mean | 113,699 µs | — |
| payload B/s | 20,137,682 | — |
| time-to-first-frame | 152,468 µs | — |
| data share | 100% | — |

Note: shim delivers 100% data share on both ports (no empty frames) because the offline bundle has no warm-up; the vendor IR port had a 3-frame warm-up (empty before frame #4). Vendor depth telemetry is unavailable for direct comparison — vendor depth returned 0 data frames in the available run.

## A/B comparison against vendor (#105/#127 records)

- Frame payload: shim [4]=Bin(2304000) matches vendor `Bin(2304000)` exactly ([hw-confirmed: 2026-10-21-plan105-127-depth-dump])
- Frame array structure: shim 11-element array matches vendor's 11-element array ([hw-confirmed: 2026-10-21-plan105-127-depth-dump])
- Element [10] (stream type): IR=UInt(4), depth=UInt(3) — matches vendor's [10]=UInt(4) on IR and UInt(3) on depth
- SET property round-trip: shim replies `vendor ACCEPT (opcode=2 reqid=N err=0, 10 B)` matching vendor's opcode=2/err=0 pattern
- start_stream: both `true` on shim vs `true` on vendor IR (vendor depth was `non-error` — not `true`)

## Crash sweep

no crash report for 'raptor_shim' at/after 2026-10-21 16:19:06 — safe to record 'no crash report (checked DiagnosticReports for raptor_shim at/after the run start)'.

## Open questions

- Why does the vendor depth port reply `start_stream -> non-error` (not `true`) while the shim replies `Bool(true)` — is this a vendor framing variant for the depth port, or a daemon state issue in that run?
- Depth bundle closing line (`[shim] depth bundle: served=N wraps=N`) was absent from the shim log (shim was SIGTERM'd before it flushed); does the depth bundle serve stat differ from the method tally (x70)?

## Board

- Row move: #172 `HW` → `DONE` — shim serves both ports with 100% data share; 11-element array, SET-property ACCEPT, start/stop_stream all match vendor protocol; no crash, no timeout; raptor_client unchanged
