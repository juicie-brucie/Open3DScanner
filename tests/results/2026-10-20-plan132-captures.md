# PLAN #132 — gap-free captures (IR + depth), post-#161/#168 fix — captures written; start_stream returned Bool(false) on both ports; data=0

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (synced to origin/main @ `2174a66`)
- Target: build/tools/raptor_client/raptor_client (build-tree copy, fixed order 2: build/tools/<target>/<target>)
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon n/a — no OBScan context; freshness: binary 22:17:03 >= newest source `capture.cpp` 20:47:37
- Interposer: n/a — exempt
- Run start: 2026-10-20 23:03:01

## Commands

```
# IR (port 10013):
./build/tools/raptor_client/raptor_client \
  --live 127.0.0.1 10013 20 --frames 200 \
  --capture ~/captures/plan132-ir \
  --capture-seconds 20 --capture-bytes 500000000 --dump-replies

# depth (port 10010):
./build/tools/raptor_client/raptor_client \
  --live 127.0.0.1 10010 20 --frames 200 \
  --capture ~/captures/plan132-depth \
  --capture-seconds 20 --capture-bytes 500000000 --dump-replies
```

## Result

- Exit code: `1` for both (frame budget: `get_frame=200 data=0 empty=200 error=0`)
- Verdict: **PARTIAL** — capture path working end-to-end (no hang, bundles written, manifests complete); `get_frame` returned 200 empty replies on both ports; `start_stream` returned `Bool(false)` on both ports
- Raw logs: `2026-10-20-plan132-ir-capture.raw.log` and `2026-10-20-plan132-depth-capture.raw.log` (same directory)
- Interposer confirmation: n/a (exempt)

## Checks

| # | Check | Result |
|---|---|---|
| 1 | IR capture: no hang, capture-start flushes, `start_stream` answered | **[PASS]** `[capture] recording...` first line; `start_stream -> non-error` (answered; no timeout) |
| 2 | depth capture: no hang, `start_stream` answered (previously 2-for-2 TIMEOUT before #161 fix) | **[PASS]** `start_stream -> non-error` (answered; no timeout) |
| 3 | Both bundles written with complete manifests | **[PASS]** IR: 1202 records, 76745 bytes; depth: 1202 records, 141745 bytes; `limits_hit: {seconds:false, bytes:false}`; 1 reply file each |
| 4 | IR `get_frame` returns data frames (Bin payloads, the row's acceptance criterion) | **[FAIL]** `data=0 empty=200` — all 200 `get_frame` calls returned empty arrays |
| 5 | depth `get_frame` returns data frames | **[FAIL]** `data=0 empty=200` — all 200 `get_frame` calls returned empty arrays |
| 6 | `start_stream` returned `true` (success indicator) | **[FAIL]** both ports returned `Bool(false)` — `start_stream -> non-error` is the client's classification (not a timeout/crash), but the daemon explicitly returned `false` |

## start_stream Bool(false) observation

Both ports returned `Bool(false)` for `start_stream` in this run. In the successful streaming run earlier this session (`2026-10-20-streaming-retry-167-161-168.md`) both ports returned `Bool(true)` and delivered data frames. The difference is session state: the successful run followed a power-cycle; this run followed a `scan_server` pipeline-stall session (which itself followed the prior raptor_client sessions). `Bool(false)` from `start_stream` is the daemon declining to open the stream — a device-state condition, not a client-side bug.

## Crash sweep

`scripts/hw-postflight.sh plan132-captures '2026-10-20 23:03:01' raptor_client` →
`hw-postflight: no crash report for 'raptor_client' at/after 2026-10-20 23:03:01 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.`

## Board

- Row move: #132 stays `HW` — the capture path mechanics are confirmed working (no hang, bundles written, manifests complete, depth port no longer times out); but the row's acceptance criterion requires consecutive data-bearing `get_frame` replies with monotonically increasing timestamps. `data=0 empty=200` on both ports in this session-state does not satisfy that criterion. A fresh device state (power-cycle, then raptor_client run before scan_server) is the prerequisite for a re-attempt.
