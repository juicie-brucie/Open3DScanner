# PLAN #140 — `raptor_client` reads the 11-element array + `--capture DIR` recorder — PASS (device-free)

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-build4
- Branch: build/plan140-frame-array-capture
- Target: build/tools/raptor_client/raptor_client (build-tree copy)
- Other copy: none
- Preflight/postflight: **not run** — `raptor_client` links no vendor SDK (socket/STL only) and every
  peer in this run is a local loopback mock, not the device. No device connection was attempted at
  any point, so no interposer and no crash-report sweep apply.
- Interposer: n/a — exempt, as for the #117, #120, #122 and #127 records
  (`2026-10-19-raptor-client-selftests.md`, `2026-10-19-raptor-client-interface-routing.md`,
  `2026-10-19-raptor-client-envelope-unwrap.md`, `2026-10-19-raptor-client-reply-dump.md`).
- Run start: 2026-10-19 (the raw log carries the exact command sequence)
- Device: **not used**. The daemon on 10013/10010 was never contacted.

## What this PR delivers

Roadmap rev 21, P1 (row #140). #131 settled the `get_frame` reply's shape: the result is the
**11-element array**, the payload is **element 4** (the primary data buffer), elements 5/7/9 are
the lengths of the three data planes, element 10 is the IR light type, and the **no-data form** is
the same array with all three buffers empty
(`docs/recon/findings/frame-codec.md` §10.3, §10.4). This PR makes the client *read* that array
and adds the recorder that keeps the bytes.

1. **The parsed summary.** A `get_frame` reply now yields `GET_FRAME -> band(N)` (element 4's
   length) or `GET_FRAME -> empty` (all three buffers empty — no-data, not an error, not a frame).
   The run ends with the per-port **data/empty share**. The `--dump-replies` output is otherwise
   untouched: the summary is one more line, after the reply's own lines.
2. **`--capture DIR`** — every reply, byte for byte, into a directory **outside the repository**:
   one file per port with length-prefixed records, plus a `manifest.json` with the run's identity
   and every file's hash. Opt-in and bounded by `--capture-seconds` / `--capture-bytes`.

## Commands

```
# 1. the reply-structure selftest, now with the parsed summary
build/tools/raptor_client/raptor_client --selftest-dump

# 2. the capture selftest — the round-trip, the manifest, the repo guard
build/tools/raptor_client/raptor_client --selftest-capture

# 3. the client's own test suite
ctest --test-dir build -R raptor_client --output-on-failure

# 4. the dump and the capture together, against a loopback mock of the daemon
#    (the mock answers a get_frame with the 11-element array, the rest with a bin)
build/tools/raptor_client/raptor_client --replay \
    docs/recon/findings/streaming-replay-payloads.hex 127.0.0.1 \
    --ir-port 20010 --depth-port 20010 --dump-replies --capture /tmp/rc-cap-record

# 5. read the bundle back
build/tools/raptor_client/raptor_client --capture-dump /tmp/rc-cap-record

# 6. the existing output is unchanged when the new flags are off: the pre-change
#    binary and this one, same mock, same capture file

# 7. a capture inside the repository is refused
build/tools/raptor_client/raptor_client --live 127.0.0.1 20010 1 /tmp/rc_frame.raw \
    --capture ./cap-inside-the-repo
```

The mock is a local loopback stand-in for the daemon: it answers with the msgpack-RPC envelope
`[1, msgid, nil, result]` (protocol-spec.md §1.1). It is test scaffolding and lives outside the
repository; the selftests' own mock is inside the client (`--selftest-dump`, `--selftest-capture`),
so the pinned shapes need no external file at all.

## Result

- Exit code: `0` for the selftests, the ctest run and the mock runs; `2` for the refused capture
- Verdict: **PASS** (device-free)
- Raw log: `2026-10-19-raptor-client-frame-array-capture.raw.log` (same directory)

## The new summary line (verbatim, from the mock run)

```
[dump] reply method=ob_rpc_get_frame0-2-12 kind=Array msgid=5 envelope=Array result=array(11) [0]=UInt(0) [1]=UInt(0) [2]=UInt(0) [3]=UInt(255) [4]=Bin(3) head=[0a 0b 0c] [5]=UInt(0) [6]=Bin(0) head=[] [7]=UInt(0) [8]=Bin(0) head=[] [9]=UInt(0) [10]=UInt(2)
[dump] get_frame raw len=23 head=[94 01 05 c0 9b 00 00 00 cc ff c4 03 0a 0b 0c 00 c4 00 00 c4 00 00 02]
[dump] GET_FRAME -> band(3)
...
[dump] tally: port 20010 data=20 empty=0 other=0 (data share 100.0%)
```

The no-data form of the same array (`[4]=Bin(0)`, `[6]=Bin(0)`, `[8]=Bin(0)`) reads `empty`; the
bare top-level bin reads `Bin(3) head=[0a 0b 0c]` — the summary says what the result *is* rather
than guessing.

## The capture layout (the contract)

```
<dir>/
  manifest.json                 the run's identity and every file's hash
  replies/port-<port>.bin       one file per port, length-prefixed records
```

The record format (little-endian throughout) — the body is exactly the tuple the row asks for
(ts, port, method, msgid, byte length, bytes) and the length prefix makes the file
self-delimiting:

```
file header (12 bytes)
  magic     4 bytes  "RCRP"
  version   u32      = 1
  port      u16      the port this file carries
  reserved  u16      = 0

records, repeated to the end of the file
  body_len  u32      the length of the body that follows
  body
    ts_ms      u64    wall clock, milliseconds since the Unix epoch
    port       u16
    method_len u16
    method     method_len bytes
    msgid      u64
    byte_len   u64
    bytes      byte_len bytes, the reply as it arrived
```

## A mock manifest (verbatim, from the run above)

```json
{
  "tool": "raptor_client",
  "format": "RCRP",
  "format_version": 1,
  "capture_dir": "/private/tmp/rc-cap-record",
  "run_start_unix_ms": 1792465630480,
  "run_end_unix_ms": 1792465631188,
  "run_start_utc": "2026-10-20T03:07:10Z",
  "run_end_utc": "2026-10-20T03:07:11Z",
  "branch": "build/plan140-frame-array-capture",
  "commit": "7c041a5c7d1079d0d53eae927d51fadfd64f08ae",
  "binary_path": "/Users/brucemackay/DevReposiMac/Open3DScanner-wt-build4/build/tools/raptor_client/raptor_client",
  "binary_sha256": "1a0e21c8f01123f02d233233d87c7a0f87235b7aa565c1816cbcb43ac0cc3424",
  "limits": {"seconds": 30, "bytes": 67108864},
  "limits_hit": {"seconds": false, "bytes": false},
  "record_count": 36,
  "bytes_written": 2617,
  "files": [
    {"path": "replies/port-20010.bin", "port": 20010, "size": 2629, "sha256": "cc07f1e26974221926a0ef535e1808003c97853396f09729638b3aad0a05fb74", "records": 36}
  ]
}
```

**The hash chain.** The manifest names the commit the capture ran from; the record's own commits follow
it, so the branch head is ahead of that sha. The manifest names the binary that made the capture
(`binary_sha256` = `1a0e21c8…`) and every file it wrote (`replies/port-20010.bin` =
`cc07f1e2…`, 2629 B, 36 records). The manifest does not cover itself, so its own hash is stated
here and recomputable:

```
bd61cf8b01f24fa77799de7e4014958b549c647eb98536751ac0244e89521fbb  /tmp/rc-cap-record/manifest.json
cc07f1e26974221926a0ef535e1808003c97853396f09729638b3aad0a05fb74  /tmp/rc-cap-record/replies/port-20010.bin
```

**The bundle is never committed.** It lives outside the repository (`/tmp/rc-cap-record` here — the
manifest records the resolved path, `/private/tmp/rc-cap-record`), and the recorder **refuses** a
directory inside a git repository — the check is in the tool, not in the operator's discipline
(see check #9).

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | The reply dump's selftest passes, with the parsed summary | **[PASS]** `the mock run's dump is byte-exact PASS` — the expected lines (including `GET_FRAME -> empty`, `GET_FRAME -> band(3)` and `GET_FRAME -> Bin(3) head=[0a 0b 0c]`) are written out by hand in the selftest and compared byte for byte |
| 2 | The tally counts data, empty and other per port | **[PASS]** `the tally counts data, empty and other per port PASS` — `data=1 empty=1 other=1 (data share 50.0%)` for the mock's three replies |
| 3 | The capture selftest passes | **[PASS]** `raptor_client_capture` `Passed`; 27 checks, 0 failures — the SHA-256 vectors, the round-trip, the manifest's hashes, the repo guard and the byte bound |
| 4 | The recorded replies read back byte for byte | **[PASS]** `the no-data reply reads back byte for byte`, `the band reply reads back byte for byte`, `the bare bin reply reads back byte for byte` — all `PASS` |
| 5 | The manifest names the run and hashes every file | **[PASS]** `the manifest names the branch`, `the manifest names the commit`, `the manifest carries the binary's sha256`, `the manifest hashes replies/port-10010.bin`, `the manifest hashes replies/port-10013.bin` — all `PASS` |
| 6 | The manifest's hashes are the real SHA-256 | **[PASS]** `sha256("abc")`, `sha256("")` and `sha256 of a million 'a'` match the standard vectors — all `PASS` |
| 7 | The reader reads each record once, in file order | **[PASS]** `the reader read every record once PASS`, `the reader's file order is the name order PASS` |
| 8 | The capture is bounded | **[PASS]** `the byte bound stops the recording PASS` (`--capture-bytes 64` stops the recorder and the manifest records `limits_hit`) |
| 9 | A capture inside the repository is refused | **[PASS]** `a capture inside a repository is refused PASS`; and end to end: `[capture] the capture directory is inside a git repository (…/cap-inside-the-repo); the bundle is never committed — choose a directory outside the repository`, exit `2`, and no directory was created. The path is **resolved first**, so a symlink cannot aim a bundle at the repository either: `a capture through a symlink into a repository is refused PASS` |
| 10 | The client's test suite passes | **[PASS]** `100% tests passed out of 4` (`raptor_client_selftest`, `raptor_client_reply_timeout`, `raptor_client_reply_dump`, `raptor_client_capture`); the whole tree: `100% tests passed out of 9` (the tree grew a test with #130's rebuild) |
| 11 | The existing output is unchanged with the new flags off | **[PASS]** `replay output, pre-change vs this build: byte-identical`; `lines with the new prefixes in the flag-off run: 0`; the run still ends `[replay] done: bin=16 empty=20 error=0` |
| 12 | The bundle reads back through the tool | **[PASS]** `[capture-dump] 36 record(s), 627 byte(s) of replies, 1 file(s)` — the same 36 records the manifest counts |

## The one thing the existing counters still say

The `[replay] done: bin=16 empty=20 error=0` line is **unchanged**: `bin` and `empty` count the
*top-level* kinds of the unwrapped result, so the 20 array-shaped `get_frame` replies land in
`empty` and the 16 bin-shaped ones in `bin`. The new `[dump] tally:` line is the one that
separates **data** from **no-data** inside the array, and it only appears with `--dump-replies`.
The old line was left alone on purpose: the hardware records parse it.

## Crash sweep

Not run — hardware-free, no vendor SDK linked, no device process involved; per the exemption stated
above and in the #117, #120, #122 and #127 records.

## What this record does **not** answer

**The live run is owed, and it is a hardware step.** This run is device-free: the daemon was never
contacted, so the *live* data/empty share is still unknown. What this PR delivers is the
instrument: the array is parsed, the no-data form is told apart from a frame, the share is
counted per port, and every reply can be kept byte for byte with a hash chain.

The live run is one command:

```
build/tools/raptor_client/raptor_client --replay \
    docs/recon/findings/streaming-replay-payloads.hex 127.0.0.1 \
    --dump-replies --capture /tmp/rc-cap-live
```

and the acceptance is that the `[dump] tally:` line names a real share for each port, and the
bundle holds the replies behind it. The live path takes the same flags.

## Open questions

- **The live share is unknown.** Whether the daemon answers "no data" most of the time (as the
  capture's single recorded reply does) or streams bands, is what the live tally will say.
- **One of the capture file's own frames is not well-formed msgpack.** The depth `send_data`
  frame with msgid 160 (`94 00 cc a0 b6 … 92 ac 47 4d 02 00 01 00 b0 00 9e 00 00 00 01 00 00
  00 40`) leaves **4 bytes of residue** under a strict msgpack read: its expected-size field
  reads as a fixint where a uint32 tag would make the frame whole. The other 35 frames consume
  completely. The replay is unaffected — the frame is sent verbatim and only its method name is
  parsed — but an extractor that re-encodes or validates the capture would trip on it. Worth a
  line in the capture's own record.
- The no-data form's element 9 is uninitialised stack on the producer's side
  (frame-codec.md §10.4), so the client never reads it; the dump prints it because the dump
  prints the array as it is.

## Board

- Row move: row #140 reads `TODO` → `DONE` in this PR — the array is parsed, the no-data form is
  told apart from a frame, the share is counted per port, the recorder writes byte-exact replies
  with a hashed manifest, and the round-trip and the mock capture both pass. **No device was
  needed and none was used**, so the row does not wait on the hardware room; the *live* share is
  a separate, hardware-owned measurement, not this row's acceptance.
