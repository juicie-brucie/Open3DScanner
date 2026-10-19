# raptor_client_selftest rerun — calling out #120's ReplayInterfaceSelftest and #122's ReplyResultSelftest explicitly — PASS

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next, HEAD `2cf83f2` ("Merge pull request #316 ... hardware: #120 DONE, #122's unwrap proven, get_frame result is an Array (#127)")
- Target: build/tools/raptor_client/raptor_client (build-tree copy)
- Preflight/postflight: not run — hardware-free, no vendor SDK, no device I/O, per prior analysis and the coordinator's instruction for this run.
- Freshness checked by hand: `git diff HEAD -- tools/raptor_client/` empty (working tree matches HEAD; the merge to `2cf83f2` only brought in docs/results, not source changes); binary mtime 2026-10-19 17:41:12 >= source main.cpp 17:41:09 — same binary used for the prior #120/#122 hardware rerun, still valid, no rebuild needed.

## Commands

```
ctest --test-dir build -R raptor_client_selftest --output-on-failure
build/tools/raptor_client/raptor_client --dry-run
```

## Result

- ctest: `Passed 0.00 sec` (Test #4: raptor_client_selftest, 100% tests passed out of 1)
- Direct binary: exit `0`
- Raw log: `2026-10-19-raptor-client-selftest-rerun.raw.log` (same directory)
- Verdict: **PASS**

The `--dry-run` path runs four selftest blocks in sequence (`main.cpp:1423-1431`): `DryRun()`, `ReplayParserSelftest()`, `ReplayDeltaSelftest()`, `ReplayInterfaceSelftest()`, `ReplyResultSelftest()`. All four blocks print under the shared `[dry-run]`/`[replay-selftest]` prefix with no sub-label distinguishing which function produced which lines in the raw output, so the mapping below was confirmed by reading each function's `check(...)` call text in `tools/raptor_client/main.cpp` and matching it line-for-line against the printed output (not inferred from naming alone).

## #120's `ReplayInterfaceSelftest()` output (verbatim, 18 checks — confirmed by matching `main.cpp:994-1079`'s `check()` text 1:1 against each printed line)

```
[replay-selftest] the captured frames parse to bytes                   PASS
[replay-selftest] reads the IR method name                             PASS
[replay-selftest] reads the depth method name                          PASS
[replay-selftest] parsing does not alter the frame bytes               PASS
[replay-selftest] extracts the IR suffix                               PASS
[replay-selftest] extracts the depth suffix                            PASS
[replay-selftest] a name with no suffix yields none                    PASS
[replay-selftest] a name that is only the token still carries it       PASS
[replay-selftest] the IR frame routes to the IR port                   PASS
[replay-selftest] the depth frame routes to the depth port             PASS
[replay-selftest] an unknown suffix is rejected                        PASS
[replay-selftest] the error names the frame and the suffix             PASS
[replay-selftest] a frame with no suffix is rejected                   PASS
[replay-selftest] a frame that is not a request is rejected            PASS
[replay-selftest] --ir-port overrides the IR port                      PASS
[replay-selftest] --depth-port overrides the depth port                PASS
[replay-selftest] the overridden IR port is used                       PASS
[replay-selftest] the overridden depth port is used                    PASS
[replay-selftest] an unknown interface name is rejected                PASS
[replay-selftest] ok (0 failure(s))
```

(19 lines total: 18 checks + the summary `ok (0 failure(s))` line.)

## #122's `ReplyResultSelftest()` output (verbatim, 8 checks — confirmed by matching `main.cpp:1080-1130`'s `check()` text 1:1 against each printed line)

```
[replay-selftest] the synthetic reply parses as an envelope            PASS
[replay-selftest] the envelope itself is not a bin                     PASS
[replay-selftest] an envelope with a payload yields the payload        PASS
[replay-selftest] an envelope with an error slot is an error           PASS
[replay-selftest] the error text is carried through                   PASS
[replay-selftest] a bare payload is returned unchanged                 PASS
[replay-selftest] a non-response array is left alone                   PASS
[replay-selftest] a timeout survives the unwrap                        PASS
[replay-selftest] ok (0 failure(s))
```

(9 lines total: 8 checks + the summary `ok (0 failure(s))` line.)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `ctest -R raptor_client_selftest` | **[PASS]** `Passed 0.00 sec` |
| 2 | `DryRun()` framing selftest (pre-existing, 5 checks) | **[PASS]** `[dry-run] ok (0 failure(s))` |
| 3 | `ReplayParserSelftest()` (pre-existing, row #105b) | **[PASS]** `[replay-selftest] ok (0 failure(s))` |
| 4 | `ReplayDeltaSelftest()` (pre-existing, row #117) | **[PASS]** `[replay-selftest] ok (0 failure(s))` |
| 5 | `ReplayInterfaceSelftest()` (#120 — suffix parsing + routing table) | **[PASS]** all 18 checks, `[replay-selftest] ok (0 failure(s))` |
| 6 | `ReplyResultSelftest()` (#122 — envelope unwrap) | **[PASS]** all 8 checks, `[replay-selftest] ok (0 failure(s))` |

## Crash sweep

Not run — hardware-free, no vendor SDK linked, no device I/O; nothing could
produce a device-process crash report from this command.

## Open questions

- None raised by this run — purely confirms the two new selftest functions
  behave correctly in isolation, which is consistent with what the
  on-hardware replay reruns already observed (correct routing, envelope
  unwrap active) but does not speak to why the daemon's actual replies
  carry no binary frame data on hardware — that remains #105/#127's open
  question, untouched by this local-only run.

## Board

- Row move: none proposed beyond what's already landed (#120 `DONE`, #122
  noted per the merge commit already on this HEAD). This run is a
  confirmation rerun at the coordinator's request, not new evidence beyond
  what the prior hardware rerun (`2026-10-19-plan120-122-replay-rerun.md`)
  already established.
