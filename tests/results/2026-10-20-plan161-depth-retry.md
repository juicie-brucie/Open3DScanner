# PLAN #161 — the depth-interface (10010) retry, post-fix — bounded timeout confirmed; the hang is gone, the depth reply is still missing

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (synced to origin/main @ `2174a66`)
- Target: build/tools/raptor_client/raptor_client (build-tree copy, fixed order 2: build/tools/<target>/<target>)
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon n/a — the target creates no OBScan context; freshness: binary 22:17:03 >= newest source `capture.cpp` 20:47:37
- Interposer: n/a — `raptor_client` links no vendor SDK (socket/STL only); exempt from the interposer and vendor-artifact rules
- Run start: 2026-10-20 22:22:25

## Command

```
./build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 8 /tmp/claude-501-plan161-depth.raw --dump-replies
```

## Result

- Exit code: `3`
- Verdict: **PASS for the bounded-wait fix (no hang); the depth interface itself still produced no `start_stream` reply**
- Raw log: `2026-10-20-plan161-depth-retry.raw.log` (same directory)
- Interposer confirmation: n/a (exempt target)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | The connection to 10010 is established | **[PASS]** `[live] connected 127.0.0.1:10010 (depth, suffix 0-2-10, pull_ms=0, per-call logging=on, timeout 5000 ms)` |
| 2 | A silent daemon on the depth interface produces a **named timeout**, not an unbounded hang (row #161's deliverable) | **[PASS]** `[dump] reply method=ob_rpc_start_stream0-2-10 kind=Timeout ...` then `[live] TIMEOUT: 127.0.0.1:10010 (depth, suffix 0-2-10) ob_rpc_start_stream0-2-10: no complete reply within 5000 ms - the connection was established and the daemon did not answer` — the process exited (code 3) in ~5 s, not killed |
| 3 | The depth interface actually streams frames (the row's original goal — `#132`'s depth capture, `#127`/`#105`'s depth half) | **[FAIL]** zero frames; the very first call (`ob_rpc_start_stream0-2-10`) never got a reply |

## Crash sweep

Not applicable — `raptor_client` links no vendor SDK and creates no device-process context; nothing here could produce a vendor crash report. (Preflight states this target is exempt from the interposer/crash-report rules.)

## On failure or block — observable + one lead

- Observable: `raptor_client --live 127.0.0.1 10010 8 ... --dump-replies` → exit 3, verbatim: `[live] TIMEOUT: 127.0.0.1:10010 (depth, suffix 0-2-10) ob_rpc_start_stream0-2-10: no complete reply within 5000 ms - the connection was established and the daemon did not answer`. The connection itself succeeds; the daemon never answers `ob_rpc_start_stream0-2-10` at all, bounded or not.
- Lead: whether the depth interface (0-2-10 / port 10010) ever answers `start_stream` under any condition is a daemon-side question, not a client-bound-wait question — the #161 fix (`tools/raptor_client/**`) is confirmed working as a timeout; the remaining gap is outside this row's wall.

## Open questions

- Is the depth interface's daemon-side `start_stream` handler ever reachable from this client (e.g. does it need a prior IR-interface session, a different parameter set, or is the depth interface simply not being served by this daemon build), or does every depth attempt time out the same way regardless of timing/parameters?

## Board

- Row move: #161 stays `HW` — the client-side bounded-wait deliverable (a named timeout instead of a hang) is confirmed, 1/1 clean on real hardware. The row's framing ("Unblocks #132's depth capture and #127/#105's depth halves") is **not** satisfied by this result: the depth interface still produced zero frames. #161's own client-side fix is done; the depth-interface-never-answers symptom is a new/separate observation for the dispatcher to route.
