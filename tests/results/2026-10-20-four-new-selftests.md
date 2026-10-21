# Rows #161/#163/#164/#167 — the four new device-free selftests — PASS

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (synced to origin/main @ `2174a66`)
- Target: no hardware target — four ctest entries, none link the vendor SDK
  (`device_probe_test`, `ws_broadcast_counters_test`, `fast_encode_test` link
  `openscanner_engine` only; `raptor_client_live_timeout` runs
  `raptor_client --selftest-live-timeout` against a loopback mock)
- Preflight/postflight: **not run** — hardware-free, no vendor SDK loaded, no
  device process, nothing a crash-report sweep could apply to
- Interposer: n/a
- Run start: 2026-10-20 22:16 (see raw log timestamps)

## Command

```
cmake -S . -B build
cmake --build build --target device_probe_test ws_broadcast_counters_test fast_encode_test raptor_client scan_server -j8
ctest --test-dir build -R 'device_probe_test|raptor_client_live_timeout|ws_broadcast_counters_test|fast_encode_test' --output-on-failure
ctest --test-dir build -LE hardware --output-on-failure
```

## Result

- Exit code: `0` for both ctest invocations
- Verdict: **PASS** (device-free)
- Raw log: `2026-10-20-four-new-selftests.raw.log` (same directory)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `device_probe_test` (row #167's bounded retry/backoff) | **[PASS]** `Passed 0.34 sec` |
| 2 | `ws_broadcast_counters_test` (row #163's per-client counters) | **[PASS]** `Passed 0.26 sec` |
| 3 | `fast_encode_test` (row #164's allocation-free encoder) | **[PASS]** `Passed 0.06 sec` |
| 4 | `raptor_client_live_timeout` (row #161's bounded wait against a silent peer) | **[PASS]** `Passed 1.03 sec` |
| 5 | Full device-free suite, no regression | **[PASS]** `100% tests passed out of 21` |

## Crash sweep

Not run — hardware-free; none of the four targets loads the vendor SDK or
touches the device, so nothing here could produce a device-process crash
report.

## Open questions

- None — all four assert device-free behaviour only; each one's actual
  hardware effect is a separate owed run (#161's depth retry, #167's live
  start re-run; #163/#164 have no outstanding hardware card on this board).

## Board

- No row move proposed by this record alone — the device-free halves of
  #161, #163, #164 and #167 were already landed `BUILD`→`HW` (or `DONE` for
  #162/#163 where applicable) by the prior dispatcher round; this run only
  confirms those selftests pass on the freshly synced tree before the
  hardware retries below.
