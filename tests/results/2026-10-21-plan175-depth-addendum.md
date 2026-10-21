# PLAN #175 — depth port telemetry addendum — FAIL

Addendum to: `2026-10-21-plan175-raptor-telemetry.md`
Date: 2026-10-21

RPCServer was restarted fresh by the coordinator. The depth port (10010) was run as the first raptor_client call in the new daemon lifetime.

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_client/raptor_client  (build-tree copy)
- Preflight: pwd OK; no conflicting process; daemon: n/a; freshness: binary 16:02:17 >= newest source main.cpp 16:02:09
- Interposer: n/a
- Run start: 2026-10-21 16:13:50

## Command

```
build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 8
```

## Result

- Exit code: `1`
- Raw log: `2026-10-21-plan175-depth-addendum.raw.log` (same directory)

## Observable

`start_stream` replied `non-error` (not `true` as IR port replies). All 62 GET_FRAME polls returned empty (kind=7). Exit code 1.

Verbatim timing line:
```
[timing] depth: n=62 clock=steady_clock p50=177us p95=302us max=2162us mean=224us; payload=0B/s wire=2435B/s; first_frame=none; timeouts=0; window=8088807us
```

Verbatim done line:
```
[live] done: get_frame=62 data=0 empty=62 error=0; stream stopped
```

## Depth Telemetry (port 10010)

| Metric | Value |
| --- | --- |
| Calls (n) | 62 |
| p50 send→reply | 177 µs |
| p95 send→reply | 302 µs |
| max send→reply | 2,162 µs |
| mean send→reply | 224 µs |
| payload throughput | 0 B/s |
| wire throughput | 2,435 B/s |
| time-to-first-frame | none |
| timeouts | 0 |
| total window | 8,088,807 µs (8.1 s) |
| frames: data/empty/error | 0 / 62 / 0 |

Note: `start_stream -> non-error` (the IR port replied `start_stream -> true`; difference in reply text is as-observed, not interpreted).

## Crash sweep

no crash report for 'raptor_client' at/after 2026-10-21 16:13:50 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## On failure — observable + one lead

- Observable: depth port (10010) exits 1 after receiving only empty frames (62/62); `start_stream -> non-error`; `first_frame=none`; `payload=0 B/s` even as the first call after RPCServer restart.
- Lead: `start_stream -> non-error` vs `start_stream -> true` on the IR port suggests the depth RPC stream-start reply differs in structure; the stream may not be activating (tools/raptor_client/main.cpp reply parsing or the depth daemon stream path).

## Board

- Row move: #175 stays `HW` — depth port connects and responds (no timeout, no crash) but delivers zero data frames; IR telemetry is complete; depth stream activation is unresolved.
