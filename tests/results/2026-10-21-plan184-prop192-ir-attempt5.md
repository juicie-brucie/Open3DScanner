# PLAN #184 — prop=192 ACCEPT/REJECT check — IR attempt 5 — FAIL

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Device serial: CR0886100SN
- HEAD SHA: 34d11e61aff07b7d206e7fb0641c686341fb236b
- Target: build/tools/raptor_client/raptor_client  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 18:21:15 >= newest source main.cpp 18:21:07
- Interposer: n/a — links no vendor SDK (socket/STL only); vendor-artifact & interposer rules do not apply
- Run start: 2026-10-21 19:50:36

## Command

```
build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 8
```

## Result

- Exit code: `0`
- Verdict: **FAIL**
- Raw log: `2026-10-21-plan184-prop192-ir-attempt5.raw.log` (same directory) — **ABRIDGED**: GET_FRAME output lines for even-numbered data frames #6, #8, #10, …, #53, #55 (26 of 56 frames) are missing; their associated SET calls are present but the GET_FRAME response line for each is absent; no summary marker was written
- Raw log sha256: `65dd07eb1eb451fef44296adca144fa100eb4ea84ba9cb6572c0099b0ce1f58a`
- Interposer confirmation: n/a

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | prop=192 anomaly absent — at reqid=192 the client sent `SET prop=152 value=0 reqid=192 -> vendor no ack (0 B)` (correct prop, correct table) | **[PASS]** `[live] SET prop=152 value=0 reqid=192 -> vendor no ack (0 B)` |
| 2 | ACCEPT or REJECT line appears for any SET | **[FAIL]** Zero ACCEPT/REJECT lines across all 280 SET calls; every SET returned `-> vendor no ack (0 B)` |
| 3 | Frame count: 56 total, data/empty breakdown | **[PASS]** `done: get_frame=56 data=52 empty=4 error=0` |
| 4 | Exit code 0 | **[PASS]** EXIT:0 |

## Crash sweep

hw-postflight: no crash report for 'raptor_client' at/after 2026-10-21 19:50:36 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## On failure or block — observable + one lead

- Observable: `build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 8`, exit code 0. All 280 SET RPCs returned `-> vendor no ack (0 B)`. No `ACCEPT` or `REJECT` line appeared anywhere in the run. The ACCEPT/REJECT branch introduced in PR #464 was not exercised by the device in this session.
- Lead: likely in the SET response handler / ACCEPT-REJECT parse path (tools/raptor_client/main.cpp) — the vendor returned 0-byte bodies for every SET, so the new branch is unreachable on this device/firmware combination.

## Open questions

- Does this device's RPCServer firmware ever return a non-empty SET ack body, or does it always send 0-byte responses regardless of the ACCEPT/REJECT fix?
- Is the ACCEPT/REJECT code path testable only via a mock/replay, not against live hardware?

## Board

- Row move: #184 `HW` → `HW` — the prop=192 aliasing anomaly is not reproducible (correct prop at reqid=192 in all 280 SETs); however the ACCEPT/REJECT branch introduced in #464 was never exercised by the device (0-byte vendor ack on every SET), so the live confirmation the row requires was not obtained.
