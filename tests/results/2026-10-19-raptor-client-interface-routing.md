# PLAN #120 — raptor_client interface routing (two-port mock daemon) — PASS

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-build3
- Branch: build/plan120-replay-routing
- Target: build/tools/raptor_client/raptor_client (build-tree copy)
- Other copy: none
- Preflight/postflight: **not run** — `raptor_client` links no vendor SDK (socket/STL only) and the
  peers here are local loopback mocks, not the device. No device connection was attempted at any
  point in this run, so no interposer and no crash-report sweep apply.
- Interposer: n/a — exempt, as for the #117 selftest record
  (`2026-10-19-raptor-client-selftests.md`).
- Run start: 2026-10-19 16:57:09 EDT (the raw log's header)
- Device: **not used**. This run never contacted the daemon on 10013/10010.

## Commands

```
# 1. the dry-run's per-interface split
build/tools/raptor_client/raptor_client \
    --replay docs/recon/findings/streaming-replay-payloads.hex --dry-run

# 2. the live replay against a two-port mock daemon (one port per interface)
python3 <two_port_daemon.py> 19950 19951 docs/recon/findings/streaming-replay-payloads.hex &
build/tools/raptor_client/raptor_client \
    --replay docs/recon/findings/streaming-replay-payloads.hex 127.0.0.1 \
    --ir-port 19950 --depth-port 19951

# 4. a stall against silent peers on both ports
build/tools/raptor_client/raptor_client \
    --replay docs/recon/findings/streaming-replay-payloads.hex 127.0.0.1 \
    --ir-port 19952 --depth-port 19953 --reply-timeout-ms 1200

# 5. an unknown interface suffix is an error naming the frame
build/tools/raptor_client/raptor_client \
    --replay <bad.hex> --dry-run        # one frame whose method ends 0-2-23

# 6. the selftests
ctest --test-dir build -R raptor_client --output-on-failure
```

The mock daemon is a two-port stand-in for the real one: each port accepts **one** connection and
replies **only** on its own, framing the captured frames per interface. A client that sent every
frame down a single connection — the behaviour row #120 fixes — cannot complete it.

## Result

- Exit codes, matching the log's five `exit:` lines (0, 1, 1, 2, 0): **0** dry-run; **1** live two-port
  mock replay (all 36 answered; the tool returns 1 when `bins == 0` — its `bins > 0 ? 0 : 1`
  convention — and the mock's replies are envelopes, not bins, so this is not a failure); **1** stall
  (deliberate); **2** unknown suffix (deliberate); **0** ctest.
- Verdict: **PASS**
- Raw log: `2026-10-19-raptor-client-interface-routing.raw.log` (same directory)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | The dry-run parses all 36 frames and reports the per-interface split | **[PASS]** `interfaces: IR 13 (span 256 ms) / depth 23 (span 211 ms)` — the 13/23 split matches the composition documented in `docs/recon/findings/streaming-replay-payloads.md` §6, which is an independent check of the suffix parse (it is not self-consistent: the tool derives the split from the method names in the bytes) |
| 2 | Each frame's dry-run line names its suffix, interface and port | **[PASS]** e.g. `frame 1: 0-2-12 (IR) -> port 10013`, `frame 2: 0-2-10 (depth) -> port 10010` |
| 3 | The IR port receives exactly the IR frames | **[PASS]** `IR(0-2-12): received all 13 frames` |
| 4 | The depth port receives exactly the depth frames | **[PASS]** `depth(0-2-10): received all 23 frames` |
| 5 | The two connections are genuinely interleaved, not sequentialised per interface | **[PASS]** the mock's arrival log alternates between the two ports (`IR: received frame 1 of 13`, `depth: received frame 1 of 23`, `depth: received frame 2 of 23`, … `IR: received frame 2 of 13`), which is the file order preserved across two connections |
| 6 | All 36 frames are answered and the run completes | **[PASS]** 36 × `frame N (suffix) -> kind=7`, then `done: bin=0 empty=36 error=0`; `kind=7` is the msgpack-RPC response envelope `[1, msgid, nil, result]` |
| 7 | A stall names the frame **and** its interface | **[PASS]** `[replay] frame 1 (0-2-12): no reply within 1200 ms`, exit 1 |
| 8 | An unknown interface suffix is an error naming the frame, not a silent default | **[PASS]** `[replay] frame 1: unknown interface suffix '0-2-23' (known: 0-2-12, 0-2-10)`, exit 2 |
| 9 | The selftests pass under ctest | **[PASS]** `raptor_client_selftest` and `raptor_client_reply_timeout` both `Passed`; `100% tests passed out of 2` |

## Crash sweep

Not run — hardware-free, no vendor SDK linked, no device process involved; per the exemption stated
above and in the #117 selftest record.

## Open questions

- **This run does not answer whether the real daemon replies on the port its suffix names.** That is
  row #120's acceptance, and it is the hardware re-run: the live `--replay` against the daemon on
  10013/10010, which must complete 36/36 or name the unanswered frame and its interface. If the
  real daemon answers a depth-suffixed frame on the IR port, the routing will show 36/36 with the
  depth frames answered on 10013 — worth recording, because it would mean the suffix is not
  load-bearing for the daemon.
- The mock frames the captured frames by their known lengths per interface. A frame whose bytes
  differ from the capture (a different file) would need the mock rebuilt; the client itself has no
  such dependency.

## Board

- Row move: row #120 reads `TODO` → `HW` in this PR — the implementation and its device-free
  checks are done, and the acceptance is the live re-run, which the hardware room owns.
