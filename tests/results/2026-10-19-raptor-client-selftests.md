# PLAN #117 — raptor_client_selftest / raptor_client_reply_timeout — PASS

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_client/raptor_client (build-tree copy; binary mtime 2026-10-19 16:23:58 >= source main.cpp 16:23:56)
- Other copy: none
- Preflight/postflight: **not run** — `raptor_client` links no vendor SDK (socket/STL only; preflight's own stated exemption) and both CTest entries are explicitly commented "Hardware-free smoke test" in `tools/raptor_client/CMakeLists.txt:5`. No device, no interposer, no crash-report sweep applicable.
- Interposer: n/a — exempt, per preflight's documented rule for this target.
- Run start: 2026-10-19 (see individual command timestamps below)
- Device: not applicable — no device connection attempted by either selftest.

## Commands

```
ctest --test-dir build -R raptor_client_selftest --output-on-failure
ctest --test-dir build -R raptor_client_reply_timeout --output-on-failure
build/tools/raptor_client/raptor_client --dry-run
build/tools/raptor_client/raptor_client --selftest-timeout
```

## Result

- Exit code: `0` for all four invocations.
- Verdict: **PASS**
- Raw log: `2026-10-19-raptor-client-selftests.raw.log` (same directory; direct-binary output, both flags)

### ctest summary

```
Test project .../build
    Start 3: raptor_client_selftest
1/1 Test #3: raptor_client_selftest ...........   Passed    0.00 sec
100% tests passed out of 1

Test project .../build
    Start 4: raptor_client_reply_timeout
1/1 Test #4: raptor_client_reply_timeout ......   Passed    0.31 sec
100% tests passed out of 1
```

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `--dry-run` framing selftest (start_stream params, vendor ReqHeader, frame response decode, vendor response magic, GVCP DISCOVERY_CMD) | **[PASS]** all 5 lines `PASS`, `[dry-run] ok (0 failure(s))` |
| 2 | `--dry-run` replay-selftest: absolute-timestamp -> delta conversion (the #117 fix) | **[PASS]** `deltas are 25259, 2, 20 ms (not 25259 three times)` — confirms the per-frame delay bug this row names is fixed in this build |
| 3 | `--dry-run` replay-selftest: monotonic/backward-stamp handling, first-frame wait cap, no-timestamp default | **[PASS]** all 7 remaining replay-selftest lines `PASS`, `[replay-selftest] ok (0 failure(s))` |
| 4 | `--selftest-timeout`: bounded reply read against silent loopback peer | **[PASS]** `connects to the silent loopback peer`, `the bounded read reports a timeout`, `the timeout fires near the deadline (not instantly, not late)` |

## Crash sweep

Not run — hardware-free, no vendor SDK linked, nothing could produce a
device-process crash report here; per the exemption stated above.

## Open questions

- These selftests confirm the parser/delay/timeout logic in isolation. PLAN
  #117's acceptance line asks for the live-replay re-run itself ("on
  hardware the replay either completes 36/36 or fails naming the exact
  frame") — that is a separate, not-yet-run step with an actual `--replay
  <file>` against the device, which this run did not attempt.

## Board

- Row move: none proposed by me — row #117 already reads `BUILD` → `HW`
  with this selftest work described as landed; I'm reporting that the
  selftests do pass on this hardware worktree. Whether that's enough to
  move the row further, or whether the live `--replay` re-run is still
  needed first, is the dispatcher's call.
