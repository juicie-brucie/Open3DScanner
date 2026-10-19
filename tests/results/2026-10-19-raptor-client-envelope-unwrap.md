# PLAN #122 — raptor_client unwraps the RPC envelope (device-free) — PASS

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-build3
- Branch: build/plan122-envelope-unwrap
- Target: build/tools/raptor_client/raptor_client (build-tree copy)
- Other copy: none
- Preflight/postflight: **not run** — `raptor_client` links no vendor SDK (socket/STL only) and every
  peer in this run is a local loopback mock, not the device. No device connection was attempted at
  any point, so no interposer and no crash-report sweep apply.
- Interposer: n/a — exempt, as for the #117 and #120 selftest records
  (`2026-10-19-raptor-client-selftests.md`, `2026-10-19-raptor-client-interface-routing.md`).
- Run start: 2026-10-19 (the raw log carries the exact timestamp)
- Device: **not used**. The daemon on 10013/10010 was never contacted.

## Commands

```
# 1. the selftests
ctest --test-dir build -R raptor_client --output-on-failure

# 2. the dry-run against the capture file
build/tools/raptor_client/raptor_client \
    --replay docs/recon/findings/streaming-replay-payloads.hex --dry-run

# 3. the replay against a mock answering [1, msgid, nil, bin(3)] on both ports
python3 <envelope_daemon.py> 19990 19991 docs/recon/findings/streaming-replay-payloads.hex &
build/tools/raptor_client/raptor_client \
    --replay docs/recon/findings/streaming-replay-payloads.hex 127.0.0.1 \
    --ir-port 19990 --depth-port 19991

# 4. the live path against a mock answering [1, msgid, nil, bin(3)]
build/tools/raptor_client/raptor_client --live 127.0.0.1 19992 2 <out.raw>

# 5. the live path against a mock answering the ERROR reply [1, msgid, "boom", nil]
build/tools/raptor_client/raptor_client --live 127.0.0.1 19993 2 <out.raw>

# 6. the same two mocks against the pre-#122 build, for the contrast
```

The mocks are local loopback stand-ins for the daemon: they answer with the msgpack-RPC envelope
`[1, msgid, nil, result]` (protocol-spec.md §1.1), the shape the real daemon uses.

## Result

- Exit code: `0` for the selftests, the dry-run and the payload-mock runs; the error-mock run exits
  non-zero by design (the client refuses to continue on a failed `start_stream`).
- Verdict: **PASS**
- Raw log: `2026-10-19-raptor-client-envelope-unwrap.raw.log` (same directory)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | The selftests pass under ctest | **[PASS]** `raptor_client_selftest` and `raptor_client_reply_timeout` both `Passed`; `100% tests passed out of 2` |
| 2 | The dry-run still parses all 36 frames and keeps #117's pacing and #120's routing | **[PASS]** `cadence: 36 frame(s), capture span 256 ms, mean gap 10.74 ms, first-frame wait 250 ms (backward stamps clamped to 0)` and `interfaces: IR 13 (span 256 ms) / depth 23 (span 211 ms)` |
| 3 | The replay counts the payload inside the envelope | **[PASS]** `done: bin=36 empty=0 error=0`, every frame `bin(3) head=[0a 0b 0c]` |
| 4 | Each interface's connection still receives its own frames | **[PASS]** `IR(0-2-12): received all 13 frames`, `depth(0-2-10): received all 23 frames` |
| 5 | The live path reports the frame payload | **[PASS]** `GET_FRAME #1 -> bin(3) head=[0a 0b 0c]`, and `start_stream -> true` |
| 6 | The live path surfaces an error reply as an error | **[PASS]** `start_stream failed: boom` — the daemon's text is shown |
| 7 | The pre-#122 build, against the same payload mock, did not | **[PASS]** (the contrast) `done: bin=0 empty=36 error=0`, every frame `kind=7` — the envelope read as an array |
| 8 | The pre-#122 build, against the same error mock, hid the error | **[PASS]** (the contrast) `start_stream -> non-error`, then `GET_FRAME #1 -> empty (kind=0)` — the error's `nil` result slot read as an empty payload |

## Crash sweep

Not run — hardware-free, no vendor SDK linked, no device process involved; per the exemption stated
above and in the #117 selftest record.

## Open questions

- **This run does not answer whether the real daemon sends a frame.** It answers what the client does
  with the replies it gets: a payload is counted, an error is reported. The device run still has to
  say which of the two the daemon actually sends for `ob_rpc_get_frame` — that is row #122's
  acceptance.
- With the unwrap, the live path can now distinguish a frame that comes back with **no payload**
  (the daemon's `nil` result — a real answer, and the `empty` bucket) from one that comes back as an
  **error** (now reported as an error, with the daemon's text). #105's "zero frames" is, for the
  first time, observable as one of those two.

## Board

- Row move: row #122 reads `TODO` → `HW` in this PR — the implementation and its device-free checks
  are done, and the acceptance is the live run on the device, which the hardware room owns.
