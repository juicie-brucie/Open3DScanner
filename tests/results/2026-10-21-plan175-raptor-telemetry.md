# PLAN #175 — raptor_client frame telemetry (IR port PASS / depth port BLOCKED) — FAIL

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_client/raptor_client  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 16:02:17 >= newest source main.cpp 16:02:09
- Interposer: n/a — links no vendor SDK (socket/STL only); vendor-artifact & interposer rules do not apply
- Run start: 2026-10-21 16:10:31

## Command

```
build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 8   # IR
build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 8   # depth
```

(IR run first; depth run immediately after)

## Result

- Exit code (IR): `0`
- Exit code (depth): `3`
- Verdict: **FAIL** (depth port timed out on start_stream; IR port completed with full telemetry)
- Raw log: `2026-10-21-plan175-raptor-telemetry.raw.log` (same directory)
- Interposer confirmation: n/a

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | IR port connects and starts stream | **[PASS]** `[live] connected 127.0.0.1:10013 (IR, suffix 0-2-12, ...)` / `start_stream -> true` |
| 2 | IR port delivers 8 data frames | **[PASS]** `done: get_frame=56 data=53 empty=3 error=0; stream stopped` (53 data frames across 56 polls) |
| 3 | IR timing line present | **[PASS]** `[timing] IR: n=56 clock=steady_clock p50=119727us p95=130739us max=143509us mean=115531us; payload=15033194B/s wire=15035512B/s; first_frame=244826us; timeouts=0; window=8122825us` |
| 4 | IR payload size | **[PASS]** `band(2304000)` per frame (2,304,000 bytes = 960×600×4 or 480×300×16; matches IR descriptor) |
| 5 | depth port connects and starts stream | **[FAIL]** `[live] TIMEOUT: 127.0.0.1:10010 (depth, suffix 0-2-10) ob_rpc_start_stream0-2-10: no complete reply within 5000 ms - the connection was established and the daemon did not answer` |

## IR Telemetry (port 10013)

| Metric | Value |
| --- | --- |
| Calls (n) | 56 |
| p50 send→reply | 119,727 µs (119.7 ms) |
| p95 send→reply | 130,739 µs (130.7 ms) |
| max send→reply | 143,509 µs (143.5 ms) |
| mean send→reply | 115,531 µs (115.5 ms) |
| payload throughput | 15,033,194 B/s (~14.3 MB/s) |
| wire throughput | 15,035,512 B/s |
| time-to-first-frame | 244,826 µs (244.8 ms) |
| timeouts | 0 |
| total window | 8,122,825 µs (8.1 s) |
| frames: data/empty/error | 53 / 3 / 0 |

## Crash sweep

no crash report for 'raptor_client' at/after 2026-10-21 16:10:31 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## On failure or block — observable + one lead

- Observable: `raptor_client --live 127.0.0.1 10010 8` exits 3 with: `[live] TIMEOUT: 127.0.0.1:10010 (depth, suffix 0-2-10) ob_rpc_start_stream0-2-10: no complete reply within 5000 ms - the connection was established and the daemon did not answer`
- Lead: the RPCServer daemon did not reply to `ob_rpc_start_stream` on the depth port (10010) after a prior raptor_client IR-port session — same pipeline-stall pattern as the post-raptor_client OBScan SDK stall; RPCServer likely needs a restart between raptor_client IR and depth runs (tools/raptor_client/main.cpp or daemon lifecycle)

## Open questions

- Does depth port 10010 time out on start_stream when called as the very first raptor_client call (no prior IR session in the same daemon lifetime)?
- Is the pipeline stall specific to the IR→depth ordering, or does depth→IR also stall?
- What is the expected depth-port telemetry (p50/p95/max, payload size, time-to-first-frame) when run cleanly?

## Board

- Row move: #175 stays `HW` — IR telemetry captured (EXIT:0, full [timing] line), depth port TIMEOUT (EXIT:3); depth-port run requires a clean daemon state (RPCServer restart needed between IR and depth raptor_client sessions)
