# frame_bins — the captured RPC frame-bin extractor (row #123)

Deterministic, offline extraction of the `ob_rpc_get_frame` payloads from a
capture, split by device port, with an index. This is the shared input for the
codec track (#124–#126): recon's container identification and the decoder read
exactly the files this writes.

## Invocation

```sh
# the practical source (the payload digest)
tools/frame_bins/frame_bins.py \
    --input ~/DevReposiMac/raptor-protocol-capture/session4/small_payloads.tsv \
    --out   ~/DevReposiMac/raptor-protocol-capture/extracted

# the pcapng fallback (TCP-reassembled; a sequence gap drops a split message)
tools/frame_bins/frame_bins.py --format pcap \
    --input ~/DevReposiMac/raptor-protocol-capture/session4/session4.pcapng \
    --out   ~/DevReposiMac/raptor-protocol-capture/extracted

# every bin result (not just get_frame), for inspection
tools/frame_bins/frame_bins.py --method all --input ... --out ...
```

`--method` defaults to `ob_rpc_get_frame`; `--min-bytes` drops tiny bins.
The output directory must be **outside the repo** — the captures are large and
are not committed.

## Output layout (the contract)

```
<out>/
  index.tsv            # one row per written bin, capture order per port
  depth/000001.bin     # port 10010, sequence 1
  depth/000002.bin
  ir/000001.bin        # port 10013, sequence 1
  ...
```

`index.tsv` columns:

| column | meaning |
| --- | --- |
| `port` | `depth` (10010) or `ir` (10013) |
| `sequence` | per-port ordinal in capture order, starting at 1 |
| `timestamp` | the capture timestamp (seconds, as the source carries it) |
| `byte_length` | the bin's length in bytes |
| `method` | the RPC method the response's msgid maps to |
| `source` | the input path |
| `file` | the bin's file name (inside `<port>/`) |
| `slot` | where the bin was found inside the result: `.` = the result itself (a top-level bin); otherwise a dotted index/key path (`4`, `6`, `8`, `4.0`) |

Determinism: the same input yields the same file names and bytes. Names carry
no timestamps, `sequence` is the capture order within a port, and bins are
written byte-for-byte as captured.

## The frame shape (why a payload is not simply "index 3")

`ob_rpc_get_frame` returns `[1, msgid, nil, [0, 0, 0, 255, bin, 0, bin, 0, bin,
u32, -1]]` — the payload is a **bin nested inside the result array** (all-empty
slots is the no-frame metadata reply). Some calls return a bin directly as the
result. The extractor **walks the result recursively** (arrays and maps, deeper
than one level) and writes **every non-empty bin**, recording the slot it came
from. Indices 4/6/8 are *not* hard-coded: on `session4` the frame lands in slot
`4` (~12 KB) while slots `6`/`8` hold tiny blobs (tens of bytes, 6 B), so a
consumer can tell a frame bin from metadata by its slot and size.

A frame is ~10–12 KB, so its message spans several TCP segments — the pcap path
reassembles contiguous segments (a sequence gap drops that message).

## Sources

- **`small_payloads.tsv`** — `timestamp \t srcport \t dstport \t hexpayload`,
  one captured payload per row (the digest caps payloads at ~1 KB). Requests
  (`[0, msgid, method, params]`) build the msgid→method map responses are
  resolved through.
- **`*.pcapng`** — LINKTYPE_NULL/IPv4/TCP; each direction is reassembled by TCP
  sequence, and a sequence gap ends the run (a message split across a gap is
  dropped, never spliced from non-adjacent bytes). The capture can be lossy, so
  the gap count matters — see the run notes in the PR.

## CI

`test_frame_bins.py` extracts `fixtures/synthetic_capture.tsv` (two frames on
10010, two on 10013, plus `send_data`/device-report decoys) and asserts the
counts, port split, order, payload bytes, index rows, and byte-identical output
on a second run. Registered as `frame_bins_test`; it runs under
`ctest -LE hardware`.
