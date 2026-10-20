# The 208-byte segments and the op-31 structures — Row #144

*Capture-only, no device. Source: `~/DevReposiMac/raptor-protocol-capture/session4/`
(`session4.pcapng`, 217 MB / 710,340 packets / 121.176 s, and its
`small_payloads.tsv` digest of 354,430 rows) plus the capture set's own
`msgpack_decode.py`. All analysis was done with the capture set's own tooling
(`msgpack_decode.py` for the msgpack framing, `tshark` for the TCP layer);
no new parser was written and no vendor binary, cache or decompiled output was
touched.*

**Every claim below is `[inferred]` — capture-derived only.** No gated hardware
run bears on any of it.

## 0. The two answers in one paragraph

1. **The 208-byte segments are NOT a second sub-stream, NOT a preview channel
   and NOT a metadata channel. They are a *capture segmentation artifact*: the
   surviving fragments of the `get_frame` reply's byte stream.** They are
   exactly 208 bytes every time, they carry no msgpack framing of their own,
   they sit on a fixed 64 KiB grid inside the frame reply, and there are
   exactly **35 of them per frame reply** — i.e. the real reply is
   **≈ 2.29 MB**, not the "~10–53 KB" the earlier spec claimed. The reason
   only the 208-byte pieces survived is the capture's own BPF filter
   (`… and less 16000`): every larger TCP segment was dropped at capture
   time, while the client's ACKs prove the bytes were really delivered.
2. **The op-31 structures are now decoded.** Structure **4034 (`0x0fc2`) is the
   device's named scan-mode table** — seven modes with names ("Laser Lines
   Scan", "Chip Scale Speckle Scan", …) — which is the thing row #114's open
   mapping was looking for. 4035 (the profile rows) is confirmed byte-exactly;
   4024 is a resolution/ROI table; 4026 and 4027 are fixed-size blobs whose
   semantics remain open. The **name → profile-row link is still not on the
   wire** (§4.1).

## 1. The 208-byte segments

### 1.1 What they are, measured

| property | 10010 → 63385 (depth) | 10013 → 63400 (IR) |
| --- | --- | --- |
| count | **146,554** | **147,022** |
| size | **208 bytes, every one** (no other size in the class) | same |
| first / last | t = 25.612480 / 111.402848 | t = 25.478524 / 111.423671 |
| bytes total | 30.48 MB | 30.58 MB |
| direction | daemon → client (same direction as the frame replies) | same |

`[inferred]` — counted from `small_payloads.tsv` (the digest keeps every payload
≤ 1 KB, so the 208-byte class is complete) and cross-checked against the pcap:
the `tcp.len==208` histogram for `tcp.srcport==10010 && tcp.dstport==63385`
gives exactly 146,554.

They appear on **only** the two frame ports. The IMU port (10011) and the RGB
port (10012) have no 208-byte class at all. `[inferred]`

### 1.2 They are not messages of their own

The RPC replies on the same connection are 26 / 72 / 519 / 1031 bytes and
carry a msgpack-RPC envelope (`94 01 <msgid> c0 …`). The 208-byte class
carries **no msgpack envelope**: it does not start with `94`/`9b`/`d9`/`da`, and
its bytes are binary (0x0F–0xFF). It is also not a `send_data` GM/RB frame
(those start `47 4d` / `52 42`). `[inferred]`

So the 208-byte block is not a standalone RPC message, and it is not the
`ob_rpc_send_data` vendor-command channel.

### 1.3 They are on a fixed 64 KiB grid of the connection's byte stream

This is the decisive measurement, and it is a *TCP-layer* fact, not a guess:

- Consecutive 208-byte segments (in arrival order) are **exactly 65,536 bytes
  apart in TCP sequence space** — e.g. raw seq 1913464924 → 1913530460 →
  1913595996 → 1913661532, each step +65,536, with 208 bytes carried each
  time. `[inferred]`
- The client **ACKs each one by exactly its own length**: the ACK goes
  1913448592 → 1913465132 (= seq + 208) → 1913530668 (= seq + 208) → … So the
  client received the 208 bytes **and** the 65,328 bytes in between; the
  missing bytes are missing from the *capture*, not from the wire. `[inferred]`
- **Why they are missing from the capture**: the capture's BPF filter is
  `(tcp portrange 10010-10013 or udp port 3956 or udp port 51984) and less
  16000` (`capinfos` on the pcapng). Every TCP segment of 16,000 bytes or
  more was dropped at capture time. The 208-byte segments are what survived
  that filter; the ~64 KiB units they head are the real frame payload. `[inferred]`
- The 10,543-byte segments on the same connection sit **immediately after**
  a 208-byte segment in sequence space (gap 0) in 3,853 of 3,897 cases — so
  the 208 bytes are *contiguous with more frame data*, not a standalone blob
  wedged between replies. `[inferred]`

Consequence: **the 208-byte segment is the head of a ~64 KiB unit of the
`get_frame` reply**, and the capture shows only that head. It is a
**segmentation artifact of the capture**, not a protocol-level object.

### 1.4 How many per frame, and how big a frame reply really is

The 29-byte client→server messages on 63385→10010 are exactly the
`ob_rpc_get_frame0-2-10` requests (wire form
`94 00 <msgid:3> b6 "ob_rpc_get_frame0-2-10" 90`; 7,232 of them in the
digest, matching row #114 §5's 7,260 within the TSV/rounding difference).
`[inferred]`

Bucketing the 208-byte blocks by the `get_frame` request that precedes them:

| 208-byte blocks in the interval | intervals |
| --- | --- |
| **35** | **3,868** |
| 0 | 2,958 |
| 19–34 (tail) | ~200 |

`[inferred]` — 35 × 65,536 = **2,293,760 bytes ≈ 2.29 MB of connection stream
per non-empty `get_frame` reply**. That is the reply's real size. The
"~10–53 KB" figure in `protocol-spec.md` §1.3 and the "167,749 segments /
72.7 MB" volume figure are **capture artifacts of the `less 16000` filter** and
need correcting (§3).

### 1.5 Pull semantics fall out of the same measurement

≈4,273 of the ≈7,232 `get_frame` calls got a ~2.29 MB payload and ≈2,959 got
**nothing at all** (no 64 KiB unit → no frame in the reply). 4,273 over the
85.8 s window = **≈49.8 frames/s**, which matches the streamed profile's
nominal **50 fps** (`[7,9,1920,1200,50]`, row #114 §3). The client polls at
~84 Hz, so **~41 % of polls return an empty result** and the daemon is
re-serving the newest frame at its own ~50 fps cadence. `[inferred]`

This is the answer to `protocol-spec.md` §6 item 3 ("pull semantics") and to
row **#143**: *the poll rate is not the frame rate; the daemon produces ~50 fps
and the extra polls come back empty.* It is a capture-derived answer, not a
gated run.

### 1.6 What the 208 bytes contain

- The byte values are confined to a narrow band: over 2,000 sampled blocks the
  high nibble is `1` in 47 % of bytes, `2` in 12 %, `3` in 9 %, `F` in 11 %; the
  low nibble is spread over 0–15 with a peak at 1. One sampled block
  contained only the byte values {0x10, 0x11, 0x12, 0x13}. The
  high-nibble and low-nibble histograms are **different shapes**, so the
  stream is *not* 4-bit-packed in the usual "two samples per byte" sense. `[inferred]`
- Successive bytes within a block differ by 0 (39 %), ±1 (30 %), ±2 (11 %) —
  i.e. the data is **spatially smooth**, like a raster image. `[inferred]`
- **The content changes with every frame**: of 3,196 consecutive frame groups,
  **0** were byte-identical; within one frame all 35 blocks are distinct; and
  146,543 of 146,554 blocks are globally unique. So the 208 bytes are
  **per-frame, per-position payload** — not a constant header, not a counter,
  not a timestamp. `[inferred]`
- A separate 10,543-byte class exists on the same connection whose bytes are
  in the 0x2B–0x39 band (printable: `,+-./0-9`) in some frames and in the
  0x10–0x1F band in others — the reply's byte stream is not homogeneous. The
  *pixel/encoding format* of the frame payload is row **#123/#124**'s subject
  (`frame-codec.md`), not this row's. `[inferred]`

## 2. The op-31 structures

### 2.0 The op-29/30/31 framing — an endianness correction

The three-step read is `INIT(29) → READ(30) → FINISH(31)`, all three as
`ob_rpc_send_data` frames on port 10010, with the structure id and (for READ)
the offset and length in the request payload.

```
request  "GM" | u16 size | u16 opcode | u16 seq | payload      (all LITTLE-endian)
response "RB" | u16 size | u16 opcode | u16 seq | u16 err | payload
```

- **`size` counts the *payload* in half-words** (e.g. a 12-byte request =
  8-byte header + 4-byte payload → `size = 2`), not the whole frame.
- **The header fields are little-endian.** `stream-profiles-row114.md` §3 says
  "the GM/RB *header* fields are big-endian; the structure payload is
  little-endian" — **that is wrong and is corrected here**: the opcode reads
  `1d 00` = 29 only as LE (`0x1d00` = 7,424 as BE), and the size reads
  `53 00` = 83 only as LE (0x5300 = 21,248 as BE — but the payload is 166
  bytes = 83 half-words). `[inferred]`
- INIT response payload: `{u16 A, u16 totalLength, u16 B, …}` — `totalLength`
  is the structure's byte length and **A = 0 for 4024/4027/4034/4035, A = 1
  for 4026**. `[inferred]`
- READ request payload: `{u32 structureId, u32 offset, u32 length}`; the
  response payload starts at **response offset 10** (after the 10-byte RB
  header) and is the structure's bytes, zero-padded to the 1,024-byte msgpack
  buffer. `[inferred]`
- FINISH response payload: 54 zero bytes. `[inferred]`

### 2.1 Structure 4034 (`0x0fc2`) — **the named scan-mode table** (the #114 mapping)

Read once in the capture, at **t = 25.046** (just before the project-open
burst at t = 25.366). Total length **364 bytes**.

It is a table of **seven records**. Each record is a **16-byte GUID**, a
**NUL-terminated name** (NUL-padded), and a **u32 code** that follows the
name's padding. The layout is fixed-width except for the name padding, so
the record boundaries are at 0, 52, 107, 158, 210, 262, 314. `[inferred]`

| # | GUID (16 B) | name (verbatim) | u32 code |
| --- | --- | --- | --- |
| 0 | `551cc6b785df988e243c449d3d38086c` | `Laser Lines Scan` | 46 |
| 1 | `4c24e2008ee8bf8a237e90d9d56ba3eb` | `Fastor Laser Lines Scan` | 46 |
| 2 | `38e1e98963ed54bb3d48e6a18ed28dd2` | `Chip Scale Speckle Scan` | 10 |
| 3 | `f480308c08d5c98241e458438b5b643a` | `Sensor Crop Speckle Scan` | 10 |
| 4 | `9b5f8317393df8be138105530b83db765` | `Sensor Binned Speckle Scan` | 10 |
| 5 | `2ed864b6443b14c5b3bcd16e714dca0f42` | `Binned Fast Speckle Scan` | 8 |
| 6 | `8749584f64ce2dc7efae9a2d9d0fc5be46` | `Factory Calibration` | 46 |

`[inferred]` — the name strings are read verbatim from the bytes; record 1's
name really does spell "Fastor Laser Lines Scan" on the wire (it looks like a
vendor name quirk — "Fastor" is probably a product name — but the bytes are
the bytes).

**This is row #114's open mapping's missing half.** The names partition
cleanly into the two families the product uses:

- the **laser-line** modes: "Laser Lines Scan", "Fastor Laser Lines Scan"
  (→ the 22-line / 7-line blue modes of #114 §8);
- the **speckle** modes: "Chip Scale Speckle Scan", "Sensor Crop Speckle
  Scan", "Sensor Binned Speckle Scan", "Binned Fast Speckle Scan"
  (→ the NIR/speckle mode);
- plus the service mode "Factory Calibration".

The u32 code is a **per-mode type/capability code** (46, 46, 10, 10, 10, 8,
46), **not** an index into the 4035 profile table (which has only 10 rows) and
not obviously an index into a profile *list* (the IR list has 54 entries, the
depth list 89). `[inferred]`

### 2.2 Structure 4035 (`0x0fc3`) — the profile rows (confirms #114 §3)

Read at **t = 25.154**, 200 bytes, **ten u32-LE 5-tuples**
`(streamType, format, width, height, fps)`. The decoded values reproduce
`stream-profiles-row114.md` §3's table **exactly**:

```
(7,9,1920,1200,50) (7,9,1520,1200,60) (7,9,1320,1040,60)
(6,9,1920,1200,50) (6,5,1920,1200,50) (6,9,1520,1200,60)
(6,5,1520,1200,60) (6,9,1320,1040,60) (6,5,1320,1040,60)
(2,5,1280,800,30)
```

`[inferred]` — row #114 §3 stands, and is re-derived here from the raw bytes
rather than from the earlier draft.

**The pairing is the key structural fact:** 4034 (the *mode list*) is read at
t = 25.046 and 4035 (the *profile rows of the selected mode*) at t = 25.154,
i.e. **the device answers "which modes exist" and then "what does the
selected mode look like"**. So the mode → profile-row mapping is *one
structure read away* from being observable; it is simply not in this capture
because the session only ever ran one mode (§4.1). `[inferred]`

### 2.3 Structure 4024 (`0x0fb8`) — a resolution / ROI table

Read at t = 2.351, 2.868 and 25.368; **110 bytes = 5 records of 22 bytes**.
Every record opens with the same four u16-LE fields:

| record | native w × h | ROI w × h | field 5 | field 6 |
| --- | --- | --- | --- | --- |
| 0 | 1280 × 800 | **480 × 300** | 3 | 43830 |
| 1 | 1280 × 800 | 1280 × 800 | 1 | — |
| 2 | 1280 × 800 | 1280 × 800 | 1 | — |
| 3 | 1280 × 800 | **960 × 600** | 3 | 48 |
| 4 | 1280 × 800 | **1920 × 1200** | 3 | 12352 |

followed by a float (1.0 in records 1–2) and zero padding. `[inferred]`

Reading: the first pair is constant across all five records and the third and
fourth are the **ROI/output sizes** — 480×300, 1280×800, 960×600 and
1920×1200, i.e. the quarter-, half- and full-scale crops of the sensor.
The fifth field (3 / 1) is a binning or crop-mode code. The **semantics of
fields 5–6 are open** (§4.3).

### 2.4 Structure 4026 (`0x0fba`) — 28 bytes

Read at t = 2.349, 2.866 and 25.366. Layout `[inferred]`:

```
u32 = 1 | u8 = 1 | u16 = 814 | u8 = 195 | u32 = 0
f32 = 2101.08 | f32 = 1.0 | f32 = 450.0 | u32 = 0
```

(The 28 bytes are byte-identical on all three reads, and the INIT's first u16
is 1 here, unlike every other structure — see §2.0.) The floats 1.0 and
450.0 are exact; 2101.08 is not a round number, so it is probably a derived
value (a range, a rate or a computed extent) rather than a constant. **No
name for this structure is established.** `[inferred]`

### 2.5 Structure 4027 (`0x0fbb`) — 760 bytes

Read at t = 2.348 and 2.865. A fixed-size table of ~7 records (760 / 7 ≈ 108;
the data repeats a **4-byte tag** at several offsets). It contains **no ASCII
strings** — it is numeric. The tags observed are `94 49 83 44` (twice, at
offsets 0 and 4), `94 49 03 45` (twice), `51 51 03 45`, `00 05 20 03`;
`00 05 20 03` also opens 4024's records, so the tag space is shared between
the two structures. Each record also carries a run of float32 values in a
plausible physical range (≈ 237 – 826) and a long zero tail. `[inferred]`

**This structure's meaning is open** (§4.3). It is *not* a name table and it
does not carry the mode names.

## 3. What this row settles, and the corrections it makes

1. **The 208-byte segments are settled**: a capture segmentation artifact of
   the `get_frame` reply, 35 per reply, reply ≈ 2.29 MB. Not a sub-stream,
   not a preview, not a metadata channel.
2. **Pull semantics (protocol-spec §6 item 3) are answered** from the same
   data: ~50 fps served, ~86 Hz polled, ~42 % of polls empty.
3. **The mode names are found** (structure 4034) — `#114` §8 OQ6 is closed;
   `#114` §8 OQ1 (the *mapping*) is narrowed but not closed (§4.1).
4. **Corrections to other documents** (applied in this PR):
   - `protocol-spec.md` §1.3 — "the observed bins are ~10–53 KB" and
     "10010→63385 = 167,749 segments / 72.7 MB" are capture artifacts; the
     reply is ≈ 2.29 MB and the volume figures describe only what the
     `less 16000` filter kept.
   - `protocol-spec.md` §6 item 2 (the 208-B open list) — closed, see §1.
   - `protocol-spec.md` §6 item 3 (pull semantics) — closed, see §1.5.
   - `protocol-spec.md` §2.1 — the GM/RB header fields are little-endian and
     `size` counts the payload's half-words, not the frame's.
   - `stream-profiles-row114.md` §3 — "the GM/RB header fields are
     big-endian" is **wrong**; they are little-endian (§2.0). §3's mode table
     is confirmed byte-exactly; §8 OQ6 is closed.

## 4. OPEN QUESTIONS

1. **The mode-name → profile-row mapping is still not on the wire.** 4034
   gives the names and 4035 gives the *selected* mode's ten rows, but the two
   are not joined by anything in this capture (the session ran one mode).
   **Recipe (no new device run needed beyond one mode-switch session):**
   capture one session that switches between the three modes and read
   structures **4034 then 4035** at each switch — 4034 identifies the mode
   list, 4035 gives that mode's rows. Whether the *u32 code* in 4034's
   records (46 / 10 / 8) is the join key is **unproven**. This is the
   remaining half of `#114` §8 OQ1 and of row **#145** (P6).
2. **Which of the seven 4034 modes is "22-line blue" vs "7-line blue" vs
   "NIR"?** The names give families ("Laser Lines Scan" vs the four "Speckle
   Scan" modes) but not the line count. Note "Chip Scale Speckle Scan" /
   "Sensor Crop Speckle Scan" / "Sensor Binned Speckle Scan" / "Binned Fast
   Speckle Scan" are **four** speckle modes where the product names only
   one NIR mode — so either the product exposes a subset, or the 4034 list is
   the *sensor's* capability list rather than the app's mode list. Open.
3. **The semantics of 4024's fields 5–6, of 4026's 8 fields, and of 4027's
   record tags.** 4024's first four u16 (native 1280×800, ROI 480×300 /
   1280×800 / 960×600 / 1920×1200) are readable; the rest is not.
4. **The exact write pattern that produced the 208-byte segments.** The
   daemon emits a 208-byte piece, then a large piece, then repeats at a
   64 KiB pitch; the surviving 10,543-byte pieces sit at offset 208 inside
   the unit in 3,853 of 3,897 cases. Whether the 208 bytes are a per-chunk
   *header* written by the daemon, or simply the tail of the kernel's MSS
   segmentation, is not settled by the capture (the segments that would
   settle it were dropped by the filter). This matters only for reproducing
   the daemon's framing, not for decoding frames.
5. **The frame payload's encoding** (the 0x0F–0x1F band, the 0x2B–0x39 band,
   and how the two relate) remains row **#123/#124**'s subject
   (`frame-codec.md`). §1.6 gives the byte-level facts the codec work needs.

## 5. Method / provenance

- `~/DevReposiMac/raptor-protocol-capture/session4/small_payloads.tsv`
  (354,430 rows) decoded with the capture set's own `msgpack_decode.py`;
  `session4.pcapng` read with `tshark` (segment lengths, TCP sequence
  numbers, ACK numbers, `capinfos` for the filter string and snaplen).
- Every number above is computed in this pass from the capture. No gated
  hardware run backs any of it: `[inferred]` throughout.
- The captures and the analysis scripts stay outside the repo
  (`~/DevReposiMac/raptor-protocol-capture/`, and a scratch directory under
  the system temp dir).
