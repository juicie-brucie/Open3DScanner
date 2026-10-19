# PLAN #127 — raptor_client's reply-structure dump (`--dump-replies`) — PASS (device-free)

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-build4
- Branch: build/plan127-reply-dump
- Target: build/tools/raptor_client/raptor_client (build-tree copy)
- Other copy: none
- Preflight/postflight: **not run** — `raptor_client` links no vendor SDK (socket/STL only) and every
  peer in this run is a local loopback mock, not the device. No device connection was attempted at
  any point, so no interposer and no crash-report sweep apply.
- Interposer: n/a — exempt, as for the #117, #120 and #122 selftest records
  (`2026-10-19-raptor-client-selftests.md`, `2026-10-19-raptor-client-interface-routing.md`,
  `2026-10-19-raptor-client-envelope-unwrap.md`).
- Run start: 2026-10-19 (the raw log carries the exact command sequence)
- Device: **not used**. The daemon on 10013/10010 was never contacted.

## Commands

```
# 1. the new selftest — the reply-structure dump, no device
build/tools/raptor_client/raptor_client --selftest-dump

# 2. determinism: the same selftest twice
build/tools/raptor_client/raptor_client --selftest-dump > a.txt
build/tools/raptor_client/raptor_client --selftest-dump > b.txt
diff a.txt b.txt

# 3. the client's own test suite
ctest --test-dir build -R raptor_client --output-on-failure

# 4. the dump in the runtime paths, against a loopback mock of the daemon
#    (the mock answers every request with the envelope [1, msgid, nil, bin(3)])
build/tools/raptor_client/raptor_client --replay \
    docs/recon/findings/streaming-replay-payloads.hex 127.0.0.1 \
    --ir-port 19990 --depth-port 19990 --dump-replies
build/tools/raptor_client/raptor_client --live 127.0.0.1 19992 1 /tmp/rc_frame.raw \
    --pull-ms 200 --dump-replies

# 5. the existing output is unchanged when the flag is off: the pre-change
#    binary and this one, same mock, same capture file
```

The mock is a local loopback stand-in for the daemon: it answers with the msgpack-RPC envelope
`[1, msgid, nil, result]` (protocol-spec.md §1.1), the shape the real daemon uses. It is test
scaffolding and lives outside the repository; the selftest's own mock is inside the client
(`--selftest-dump`), so the pinned shapes need no external file at all.

## Result

- Exit code: `0` for the selftest, the ctest run and both mock runs
- Verdict: **PASS** (device-free)
- Raw log: `2026-10-19-raptor-client-reply-dump.raw.log` (same directory)

## What the dump prints

One line per reply, plus a second line for a `get_frame` reply:

```
[dump] reply method=<name> kind=<result kind> msgid=<n> envelope=<envelope kind> result=<shape>
[dump] get_frame raw len=<bytes> head=[<hex, 48 B>]
```

`result=` is the **unwrapped** result (the #122 unwrap): for an array it is the array's length
followed by every element's kind and value, and for a bin or a string the byte length and a hex
head (32 B). The second line is the reply's own bytes as they arrived, so the envelope's shape
sits next to the unwrapped result. The lines carry no clock and no address: the same run prints
the same bytes.

## The pinned shapes — the dump lines, verbatim

The three shapes the row asks for, from the selftest's mock run (the first reply reproduces the
recorded reply head byte for byte, so the raw line below *is* the repo's recorded message):

```
[dump] reply method=ob_rpc_get_frame kind=Array msgid=5 envelope=Array result=array(11) [0]=UInt(0) [1]=UInt(0) [2]=UInt(0) [3]=UInt(255) [4]=Bin(0) head=[] [5]=UInt(0) [6]=Bin(0) head=[] [7]=UInt(0) [8]=Bin(0) head=[] [9]=UInt(2731083288) [10]=Int(-1)
[dump] get_frame raw len=24 head=[94 01 05 c0 9b 00 00 00 cc ff c4 00 00 c4 00 00 c4 00 ce a2 c9 06 18 ff]
[dump] reply method=ob_rpc_get_frame kind=Array msgid=6 envelope=Array result=array(11) [0]=UInt(0) [1]=UInt(0) [2]=UInt(0) [3]=UInt(255) [4]=Bin(3) head=[0a 0b 0c] [5]=UInt(0) [6]=Bin(0) head=[] [7]=UInt(0) [8]=Bin(0) head=[] [9]=UInt(2731083288) [10]=Int(-1)
[dump] get_frame raw len=27 head=[94 01 06 c0 9b 00 00 00 cc ff c4 03 0a 0b 0c 00 c4 00 00 c4 00 ce a2 c9 06 18 ff]
[dump] reply method=ob_rpc_get_frame kind=Bin msgid=7 envelope=Array result=Bin(3) head=[0a 0b 0c]
[dump] get_frame raw len=9 head=[94 01 07 c0 c4 03 0a 0b 0c]
```

Line 1 is the recorded **"no frame"** shape — the 11-element array whose bin-shaped slots are all
empty (the message in `docs/recon/findings/streaming-replay-payloads.md` §5). Line 3 is the same
array with a payload in the first bin-shaped slot. Line 5 is the **top-level bin** reply.

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | The new selftest passes under ctest | **[PASS]** `raptor_client_reply_dump` `Passed`; `100% tests passed out of 3` (`raptor_client_selftest`, `raptor_client_reply_timeout`, `raptor_client_reply_dump`) |
| 2 | The recorded reply parses, and its result is an 11-element array | **[PASS]** `the recorded reply parses as an envelope` / `the recorded reply's result is an array` / `the recorded reply's result has 11 elements` — all `PASS` |
| 3 | The recorded message is the envelope around the empty-bin array | **[PASS]** `the recorded message is the envelope around the empty-bin array PASS` — the 24 recorded bytes are rebuilt byte for byte |
| 4 | The dump line for the recorded reply is exact | **[PASS]** `the recorded reply dumps as an 11-element array PASS` — the expected line is written out by hand in the selftest and compared byte for byte |
| 5 | The same three lines come out of the runtime path | **[PASS]** `the mock run's dump is byte-exact PASS` — the loopback mock answers three requests with the three shapes and the captured dump is compared against the hand-written expectation |
| 6 | The dump is deterministic | **[PASS]** `two runs: byte-identical (diff is empty)`; and `dump lines carrying a clock or an address: none` |
| 7 | The dump works in the replay path | **[PASS]** 56 dump lines for the capture's 36 frames (36 reply lines + 20 `get_frame` raw lines) — e.g. `[dump] reply method=ob_rpc_get_frame0-2-12 kind=Bin msgid=5 envelope=Array result=Bin(3) head=[0a 0b 0c]` |
| 8 | The dump works in the live path | **[PASS]** `[dump] reply method=ob_rpc_get_frame kind=Bin msgid=2 envelope=Array result=Bin(3) head=[0a 0b 0c]` and its `[dump] get_frame raw len=9 head=[94 01 02 c0 c4 03 0a 0b 0c]` |
| 9 | The existing output is unchanged with the flag off | **[PASS]** `replay output, pre-change vs this build: byte-identical`; `dump lines in the flag-off run: 0`; the run still ends `[replay] done: bin=36 empty=0 error=0` |
| 10 | The msgpack reader takes a fixstr of 16-31 bytes | **[PASS]** `a 15-byte string decodes`, `a 17-byte string decodes`, `a 31-byte string decodes`, `a 32-byte string decodes` — all `PASS` |

## The one behaviour change outside the dump

The msgpack reader's fixstr test was `(b & 0xf0) == 0xa0`, which covers only the 0xa0-0xaf
lengths (0-15). A fixstr of 16-31 bytes (0xb0-0xbf) fell through to the unhandled case, so a
reply carrying one read as `unhandled msgpack byte 177` — and the daemon's own method names are
17-19 bytes long. The mask is now `(b & 0xe0) == 0xa0`, which **only widens** what parses: every
byte that parsed before parses the same way. The four checks in #10 pin it.

This matters for the row: the dump's job is to show what is inside the result array, and an
array element could be a string of that length.

## Crash sweep

Not run — hardware-free, no vendor SDK linked, no device process involved; per the exemption stated
above and in the #117, #120 and #122 selftest records.

## What this record does **not** answer

**The live confirmation is owed and is a hardware step.** This run is device-free: the daemon was
never contacted, so the array's *live* contents are still unknown. The mock answers with shapes
this repo already holds — the recorded "no frame" array, the same array with a payload, and the
top-level bin — and shows that the dump renders each of them exactly. What the hardware run has
to add is the daemon's own answer: which of those shapes (or another) the live `get_frame` reply
actually carries, and whether the bin-shaped slots hold the frame.

The hardware run is one command:

```
build/tools/raptor_client/raptor_client --replay \
    docs/recon/findings/streaming-replay-payloads.hex 127.0.0.1 --dump-replies
```

and the acceptance is that the `get_frame` dump lines name the array's contents. If the live
array's bin slots are non-empty, the dump prints their length and a 32-byte hex head — enough for
#124's step 0 ("settle the result's msgpack shape") to name the payload's slot and offset.

## Open questions

- Which slot of the live array carries the frame payload, and do the other two bin slots ever
  hold a second channel? The dump answers this only once the daemon's own reply is in it.
- The live run's `get_frame` reply may carry a string of 16-31 bytes; before the reader fix that
  read as an error, so the dump would have shown `kind=Error` instead of the array. The fix is in
  this build, so a live `unhandled msgpack byte` line now means something else entirely.
- The reader still has no map support: a fixmap (0x80-0x8f) is read as a string, and there are no
  0xde/0xdf cases. Nothing observed needs it, and this row did not change it — but a reply
  carrying a map would misparse rather than fail loudly.

## Board

- Row move: row #127 reads `TODO` → `HW` in this PR — the diagnostic and its device-free checks
  are done, and the acceptance is the live run on the device, which the hardware room owns.
