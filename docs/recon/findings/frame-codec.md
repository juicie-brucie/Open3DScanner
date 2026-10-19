# Frame container identification — what is inside the `get_frame` bin? (row #124)

*Status: **MEASURED — the container is named.** The plan's T1–T6 were run on a
corpus of **n = 11** complete `get_frame` replies (**10 depth, 1 IR**) extracted
from `session4.pcapng` with the fixed extractor (§1). The container is the
**msgpack-RPC reply envelope**; the frame payload it carries is
**headerless raw 8-bit image samples** (row stride **1920 B**).*

*§10 (added by row #131) is the **producer side** of this file: what the
daemon's code writes into the 11-element array, what every element means, and
what the serializer does **not** do. Read §10 before acting on §2.*

*This is a **sample, not a stream.** Every count below is over **n = 11**
replies unless a different n is stated. "≥99 %" on this corpus means
**11/11**. The corpus holds 11 of the **5,886** `get_frame`-shaped replies
visible in the capture; the other 5,875 carry no payload at all (§4.3).*

*Wall: `docs/recon/**`. This row does not touch
`engine/include/openscanner/obscan_api.h` — the frame container is a **wire**
format, not part of the recovered C ABI.*

*Tag convention: every substantive claim below is `[inferred]` — this row is a
**static** measurement of a recorded capture (`session4.pcapng`, a real
hardware session), with **no hardware run** in this row, so no `[hw-*]` tag
applies. Numbers are quoted verbatim from the run output.*

---

## 0. The question

`ob_rpc_get_frame` answers with a payload of ~12.2 KB for a 1920×1200 stream
(`protocol-spec.md` §1.3). Raw 8-bit pixels would be 2,304,000 B, so the
payload is *not* the whole image — but **what** it is, is now settled: the
payload is **raw image samples**, not an encoded image, and the "container"
around it is the RPC envelope itself, not a nested image file.

**Answer in one line:** the frame container is the **msgpack-RPC reply
envelope** (§2.1, byte-exact), and the frame payload inside it is
**headerless raw 8-bit pixels with a 1920-byte row stride** (§2.2) — there is
no magic, no length field, no dims and no format *inside* the payload (§3.6).

---

## 1. How the corpus was produced

### 1.1 The extractor — head cited

`tools/frame_bins/frame_bins.py` is **not** on `origin/main` in the form the
run needs: `origin/main`'s copy (last touched by `6a89e06`, "PLAN #123") has
**no `iter_bins`** and **no `94 00`/`94 01` signature scan** — it classifies a
response only when the envelope's index 3 is a *top-level bin*, and
`get_frame`'s result is an **array**, so it yields nothing (§1.2 below).

The run therefore used the fixed extractor from **branch
`build/plan128-multisegment` @ `6e13eb2`** (PR #320), copied to a temp path
and run from there. **No parser was written for this row.**

| item | value |
| --- | --- |
| extractor | `tools/frame_bins/frame_bins.py` @ `6e13eb2` (branch `build/plan128-multisegment`) |
| sha256 | `2e751ff16c75f947220112a02eba5b9dc0d420632d68d04b8ebe1faae5c5435a` |
| copy run from | `/tmp/recon124/frame_bins.py` (outside the repo) |
| command | `python3 frame_bins.py --input ~/DevReposiMac/raptor-protocol-capture/session4/session4.pcapng --format pcap --method ob_rpc_get_frame --out /tmp/recon124/out` |
| output | `/tmp/recon124/out/{index.tsv,depth/,ir/}` (outside the repo) |

Extractor output, verbatim:

```
  10010 depth responses=6162   written=30     bytes=122886
  10013 ir    responses=3170   written=3      bytes=12313
  largest bin result: 12276 B (ob_rpc_get_frame0-2-12)
  index: /tmp/recon124/out/index.tsv (33 rows)
```

### 1.2 What the 33 rows are

`index.tsv`'s 8th column is the `slot` — the position of the bin inside the
result array. 33 rows = **11 replies × 3 bins**:

| slot | what | n | size |
| --- | --- | --- | --- |
| 4 | **the frame payload** | 11 (10 depth + 1 IR) | 12,118 – 12,276 B |
| 6 | an auxiliary buffer | 11 | 19 – 168 B |
| 8 | a 6-byte blob | 11 | exactly 6 B |

The frame bins are the slot-4 rows: **depth** 12,250 / 12,257 / 12,250 /
12,243 / 12,244 / 12,240 / 12,234 / 12,241 / 12,121 / 12,118 B and **IR**
12,276 B. Slots 6 and 8 are **not** frames (§2.3).

### 1.3 The corpus-size note

The sample is small and it is a *sample*:

* **11** complete `get_frame` replies carry a payload; **5,875** other
  `get_frame`-shaped replies in the same capture carry **none**
  (§4.3) — so 11/11 is a large *share* of the payload-bearing replies and a
  small share of the calls.
* The rotated captures (`00019..00023`) contain **none** of them.
* ~76 MB of ~1.09 GB was captured on the depth connection (~93 % missing), so
  most large segments in the capture are continuation/raw, not messages.
* **n = 1 on the IR interface.** Every IR number below is a single
  observation; it is reported, but nothing is generalised from it.

---

## 2. The container — byte-exact

### 2.1 The reply envelope (the container)

The reply is the msgpack-RPC envelope `[1, msgid, nil, result]` with
`result` = **fixarray(11)** (`0x9b`). The frame payload is **element 4** of
that array. Byte-exact layout, from the wire bytes:

```
 94 01 <msgid>            msgpack response: [1, msgid, nil, result]
 c0                       nil
 9b                       result = fixarray(11)
   00                     e0   fixint   1 B    0                     (11/11)
   cf <8 B>               e1   uint64   9 B    device timestamp, µs  (11/11)
   ce <4 B>               e2   uint32   5 B    opaque counter        (6/11 nonzero)
   cc ff                  e3   uint8    2 B    255                   (11/11)
   c5 <2 B>               e4   bin16    3 B    N = the frame payload
   <N bytes>                                the payload — raw pixels, no header
   cd <2 B>               e5   uint16   3 B    N                     (11/11)
   c4 <1 B>               e6   bin8     2 B    M = auxiliary buffer
   <M bytes>
   <1 B>                  e7   fixint   1 B    M                     (11/11)
   c4 06                  e8   bin8     2 B    a 6-byte blob
   <6 bytes>
   06                     e9   fixint   1 B    6                     (11/11)
   <1 B>                  e10  fixint   1 B    0 (depth) / 2 (IR)
```

`msgid` is written with msgpack's **minimal** width: `uint8` when < 256
(depth msgids 209–244 → 2 B), `uint16` otherwise (IR msgid 5220 → 3 B). The
same minimal-width rule applies to `e2`, which is a **fixint 0** in 5/10
depth replies and a `uint32` otherwise — a decoder must not assume a width.

**Two envelope forms exist for the same method.** `ob_rpc_get_frame` answers
either with a frame (above) or with the **no-data form**, 26 B:

```
9401cd1c61c09b000000ccffc40000c40000c400cea2c90018ff
  e0=0 e1=0 e2=0 e3=255 e4=bin(0) e5=0 e6=bin(0) e7=0 e8=bin(0) e9=uint32 0xA2C90018 e10=-1
```

(msgid 7265, depth, method `ob_rpc_get_frame0-2-10`.) **In the no-data form
`e9` and `e10` carry different types** (`uint32` and `int8 −1`) than in the
data form (`fixint` 6 and `fixint` 0/2) — the reply is a **variant**, not a
fixed-layout struct, and the decoder must switch on it (§7, OQ 3).

### 2.2 The payload — headerless raw 8-bit pixels

The payload carries **no header of its own**. The only thing that delimits
it is the msgpack `bin16` length in `e4`. The evidence that it is raw pixel
data, not an encoded image:

| measurement | depth (n = 10) | IR (n = 1) |
| --- | --- | --- |
| adjacent-byte \|Δ\|, mean | 1.99 – 6.22 | 0.25 |
| adjacent-byte pairs that are **equal** | 12.6 % – 67.9 % | 79.8 % |
| normalised autocorrelation, **lag 1920** | **+0.990 … +0.999** | **+0.972** |
| same, lag 1 | +0.977 … +0.998 | +0.978 |
| same, lag 120 / 480 / 960 | −0.570 … +0.769 | −0.028 / −0.026 / −0.024 |
| adjacent rows (k vs k+1), mean \|Δ\| | 1.70 – 7.13 | — |
| adjacent rows, equal bytes | 12 % – 68 % of 1920 | — |

**lag 1920 is the single strongest lag in 10/10 depth frames and 1/1 IR
frame** (the top lag of 2…4096 for the frame tested). A compressed stream
has no such periodicity. The **1920-byte period is the row stride**, and
1920 is exactly the profile's width (`protocol-spec.md` §7) — the container's
payload agrees with the streamed profile's width by measurement, not by
assumption.

The 0xFF (invalid) pixel bands repeat with period **exactly 1920** in **10/10**
depth frames, e.g. seq 1 at offsets 616 / 2536 / 4455 / 6387 / 8307 / 10227 /
12147 (column 616–627 in every row, drifting 11 columns over 6 rows).

### 2.3 The companions (slots 6 and 8) — not frames

| slot | depth | IR |
| --- | --- | --- |
| 6 | 32, 19, 36, 33, 36, 46, 48, 45, 165, 168 B — pixel-like values in the same band as the payload | 31 B, values 0x0F–0x13, the same band as the IR payload |
| 8 | `ff ff ff ff ff ff` in **10/10** | `b1 1b 00 00 99 03` |

Slot 6 is *the same kind of data* as the payload at a much smaller size; slot 8
is a fixed 6-byte field that differs per interface. **Neither is promoted to
"frame" here** — there is no evidence they carry pixels, and their meaning is
open (§7, OQ 2).

---

## 3. T1–T6, per interface (the plan's §2, run)

### 3.0 Step 0 — the shape of the answer: **settled**

The result element is an **11-element array** (§2.1), and the **payload is in
slot 4** — a `bin16` of 12,118–12,276 B in 11/11. The array is the frame
*envelope*; the payload's first bytes are at a **known offset** (immediately
after the `c5 <2 B>` length), so T1/T2 could be run at the right place.

The `94 00`/`94 01` signature scan in the fixed extractor finds the reply
header, and the `iter_bins` walk finds the nested bins — the plan's OQ 1 is
answered.

### 3.1 T1 — magic / first-bytes scan

| | depth (n = 10) | IR (n = 1) |
| --- | --- | --- |
| standard signature at offset 0 | **NONE** (10/10) | **NONE** (1/1) |
| distinct first-4-byte heads | **10 of 10** (every frame differs) | 1 (n = 1) |
| `ftyp` within the first 64 B | 0/10 | 0/1 |

Signatures tried: JPEG `FF D8 FF`; PNG `89 50 4E 47 0D 0A 1A 0A`; zlib
`78 01/9C/DA/5E`; gzip `1F 8B`; LZ4 `04 22 4D 18`; Zstd `28 B5 2F FD`; LZMA
`5D 00 00`; xz `FD 37 7A 58 5A 00`; BMP `42 4D`; TIFF `49 49 2A 00` /
`4D 4D 00 2A`; ZIP `50 4B 03 04`; H.264/HEVC Annex-B `00 00 00 01` /
`00 00 01`; RIFF. **Zero hits on 11/11.**

### 3.2 T2 — standard-decoder trials

| decoder | depth (n = 10) | IR (n = 1) |
| --- | --- | --- |
| JPEG (SOF parse) | **0/10** | **0/1** |
| PNG (IHDR parse) | **0/10** | **0/1** |
| zlib (wbits 15) | **0/10** | **0/1** |
| gzip (wbits 31) | **0/10** | **0/1** |
| raw deflate (wbits −15) | **0/10** | **0/1** |
| HEIF / `ftyp` | **0/10** | **0/1** |
| H.264/HEVC Annex-B | **0/10** | **0/1** |
| LZ4 frame | **0/10** | **0/1** |
| Zstd | **0/10** | **0/1** |
| xz | **0/10** | **0/1** |
| BMP | **0/10** | **0/1** |
| TIFF | **0/10** | **0/1** |

External probes on the first depth and the first IR bin, verbatim:

```
sips       rc=0   ... | pixelWidth: <nil> | pixelHeight: <nil>
ffmpeg     rc=183 Error opening input: Invalid data found when processing input
zstd -d    rc=1   zstd: unsupported format
lz4 -d     rc=44  Error 44 : Unrecognized header : file cannot be decoded
```

**No standard decoder succeeds on any of the 11.** The MJPEG prior (§1.3 of
the old plan) is **not** borne out: the device's format id 9 does not imply
an MJPEG payload on the wire.

### 3.3 T3 — entropy

| | depth (n = 10) | IR (n = 1) |
| --- | --- | --- |
| per-frame Shannon entropy | 2.9613 – 7.2204 bits/byte (mean 4.2065) | 0.7559 bits/byte |
| all frames concatenated | **4.8699** bits/byte | 0.7559 |
| 0xFF mass | 47.103 % | 0.000 % |
| zero mass | 0.008 % | 0.000 % |
| distinct byte values | 240/256 | 35/256 |
| top byte | `ff` 47.103 % | `10` 87.382 %, then `11` 9.686 % |
| χ² vs uniform (255 dof) | 6,897,733.6 | 2,417,235.2 |

Entropy is **below** 7.9 bits/byte on 11/11 → the payload is **not** compressed
or obfuscated. The IR frame is 87.4 % one byte value — a near-uniform 8-bit
image, exactly what raw sensor data looks like.

### 3.4 T4 — size vs content

| | depth (n = 10) | IR (n = 1) |
| --- | --- | --- |
| sizes (B) | 12,118 – 12,257, mean 12,219.8, stddev 50.5 | 12,276 |
| p50 / p95 | 12,243 / 12,250 | 12,276 / 12,276 |
| coefficient of variation | **0.0041** | 0.0000 |
| ratio to 1920×1200×1 B | 0.00530 (**×188.5**) | 0.00533 (×187.7) |
| ratio to 1920×1200×2 B | 0.00265 (×377.1) | 0.00266 (×375.4) |
| consecutive-pair bytes differing | 0.4410 – 0.9979 | n/a (n = 1) |
| `len mod stride` constant? | **no** for 8/12/16/24/28/32/48/64 | n/a |

The **stride check fails** for every plausible stride — but that is now
*expected*, because the payload is a **band of image rows**, not a
point/element array: the H3 falsifier ("length mod stride is not constant")
fires, and H3 is refuted on other grounds (§4).

The 44 %–99.8 % consecutive-frame byte-difference rate is **not** the
"fixed-rate video codec" signature the plan's decision table anticipated: the
difference *magnitude* is small (within-frame mean \|Δ\| 1.99–6.22), the
autocorrelation peak at lag 1920 is present in every frame, and consecutive
rows inside a payload are near-identical. It is a **moving scene**, not
inter-frame coding.

### 3.5 T5 — header sharing between consecutive frames

| | depth (n = 10) | IR (n = 1) |
| --- | --- | --- |
| longest common prefix, consecutive pairs | **0** on all 9 pairs | n/a |
| pairs sharing the first 4 / 8 / 16 / 32 B | **0/9 / 0/9 / 0/9 / 0/9** | n/a |
| corpus-wide common prefix | **0 bytes** | 12,276 B (n = 1, meaningless) |
| all-pairs LCP matrix | 0 everywhere except 1 off-diagonal 1 | n/a |

**No per-frame header exists** — T5's "constant K ≥ 8" outcome does not
occur; the payload is monolithic from byte 0.

### 3.6 T6 — self-describing structure scan

Scanned at every offset 0…63, for the profile's constants **1920 / 1200 /
50 / 9** as u16 and u32, little- and big-endian, and for a self-length field
(u16/u32 LE/BE equal to the bin's total or remaining length):

| scan | depth (n = 10) | IR (n = 1) |
| --- | --- | --- |
| known-constant (1920, 1200, 50, 9) | **0 hits** | **0 hits** |
| self-length | **0 hits** | **0 hits** |

**The payload does not describe itself.** The container's dims and format are
**not in the frame reply at all** — they live in `start_stream`'s params
`[streamtype, 9, 1920, 1200, 50]` (`protocol-spec.md` §7). The one dimension
that *is* recoverable from the payload is the **width 1920**, and it comes
from the payload's own row periodicity (§2.2), not from a field.

### 3.7 Byte accounting

| | depth (n = 10) | IR (n = 1) |
| --- | --- | --- |
| `e4`'s `bin16` length == the index's `byte_length` | **10/10** | **1/1** |
| `e5` == `len(e4)` | 10/10 | 1/1 |
| `e7` == `len(e6)` | 10/10 | 1/1 |
| `e9` == `len(e8)` | 10/10 | 1/1 |
| the 11 elements' bytes + the envelope == the measured message length | **10/10** (exact) | **1/1** (exact) |

The message length is `6 + parts` on depth (msgid 2 B) and `7 + parts` on IR
(msgid 3 B): e.g. depth reply 1 = `36 + 12,250 + 32` parts + 6 = 12,324 B
(measured 12,324); IR = `36 + 12,276 + 31` + 7 = 12,350 B (measured 12,350).
**The accounting closes exactly, with zero residue, on 11/11.**

---

## 4. The container, and the completeness question

### 4.1 The container is named

**The frame container is the msgpack-RPC reply envelope** (§2.1): an
11-element fixarray whose element 4 is a `bin16` carrying the frame payload.
There is **no nested image container** — the payload has no magic, no header,
no dims, no format, no length field of its own (T1 11/11, T5 9/9, T6 11/11).

### 4.2 The payload is a *band*, not the frame

12,250 B at a 1920-byte stride is **6 rows + 730 columns** of a 1920-wide
image. The streamed profile is 1920×1200 = 2,304,000 B — **×188.5**.

The decisive check: over **5,886** `get_frame`-shaped replies in the whole
capture (2,844 depth + 3,042 IR), the **declared** length of `e4` is:

| declared `e4` length | replies |
| --- | --- |
| 0 (no data form) | **5,875** |
| 12,118 / 12,121 / 12,234 / 12,240 / 12,241 / 12,243 / 12,244 / 12,250 ×2 / 12,257 / 12,276 | **11** (the corpus) |

**The maximum payload length the daemon ever declares in this capture is
12,257 B (depth) / 12,276 B (IR).** The daemon never declares 2,304,000 B.
So the daemon returns the image in **~12 KB bands, one per `get_frame`
reply**, and the client assembles the 1920×1200 frame from many replies.
`get_frame`'s request carries **empty params** (`90` = fixarray(0)) on both
interfaces, so the split is the daemon's, not the client's.

### 4.3 What the assembly rule still needs

Each payload holds 6 whole rows **plus a partial row** (e.g. 730 of 1920
columns), and the 0xFF invalid bands repeat with period 1920 inside it. So
each band starts on a row boundary but ends mid-row — the rule that stitches
bands into a 1920×1200 image is **not** in this corpus: the 11 replies are
from different moments with different scenes (the camera was moving), so
consecutive bands cannot be phase-matched against each other. **This is the
one thing the next capture must settle** (§6).

---

## 5. Hypothesis verdicts

| hypothesis | verdict | evidence |
| --- | --- | --- |
| **H1 — standard codec** (JPEG/PNG/zlib/HEVC/LZ4/Zstd/…) | **FALSIFIED** | 0/10 + 0/1 for all 12 decoders and 4 external tools (§3.2); 0/11 for all 18 signatures (§3.1); entropy < 7.9 bits/byte on 11/11 (§3.3) |
| **H2 — vendor container** (proprietary header + body) | **SURVIVES — and is now resolved** | the container *is* the RPC envelope, byte-exact (§2.1); but it is **headerless inside**, so this resolves to "vendor envelope + raw body", not "vendor image format" |
| **H3 — sparse point / measurement data** | **FALSIFIED** | `len mod stride` is not constant for 8/12/16/24/28/32/48/64 (§3.4); the lag-1920 autocorrelation peak and the row-to-row similarity are image structure, not a point array (§2.2) |
| **H4 — uncompressed raw pixels** | **KEPT — and confirmed** | the payload **is** raw 8-bit pixels: lag-1920 autocorrelation is the top lag in 10/10 + 1/1; 12.6–67.9 % of adjacent bytes are equal; adjacent rows are near-identical; 0xFF invalid bands recur every 1920 B (§2.2). The plan's H4 as written ("bin length == w×h×bpp") is **wrong in detail**: the bin is a *band*, not the frame (§4.2) |
| **H5 — obfuscated / encrypted payload** | **FALSIFIED** | entropy 2.96–7.22 bits/byte with structure; a constant header was *not* found because there is no header (T5 LCP = 0), and T6 found no self-describing field — but the payload is plainly not opaque: it is a 1920-stride image |

**One hypothesis survives: H4, refined.** The frame is **not** a compressed
image in a container; it is **raw 8-bit image samples** carried in the
vendor's RPC envelope, in ~12 KB bands.

---

## 6. Acceptance, at the sample's scale (n = 11)

| clause | result |
| --- | --- |
| (1) one container named — a standard codec with a named decoder on ≥99 %, **or** a vendor container with a byte-exact header layout holding on ≥99 % with zero declared-length exceptions | **MET** — the msgpack-RPC frame envelope, byte-exact (§2.1), holding on **11/11**, with **zero** declared-length exceptions (§3.7). No standard codec exists, so the second limb applies. |
| (2) the header's width / height / format agree with the streamed profile (1920×1200, format 9) on ≥99 % | **PARTIALLY MET — 2 of 3 fields, and the third is absent by construction.** The container carries **no** width/height/format field at all (T6: 0 hits, 11/11). The **width 1920 is corroborated by measurement** from the payload's row stride (11/11); the **pixel format is 8-bit single-channel** (1 byte per pixel: 1920 B per row, and 0xFF invalid runs of **28/29 B** — odd, so single-byte markers, not 0xFFFF pairs), which is **Y8 = the SDK's format id 9** `[inferred]`, contingent on the §7 OQ 2 (the ids may be the device's own). The **height 1200 is not in the container** — it is a fact of `start_stream`'s params. |
| (3) the byte accounting closes — header + payload = the message, no unexplained residue — on ≥99 % | **MET** — exact on **11/11**, zero residue (§3.7). |
| (4) if vendor-specific, the specific next evidence is named | **MET** — §7. |

**2 clauses met, 1 partially met, 1 met.** The acceptance is **not** fully
met: clause (2) cannot be met by a container that carries no dims/format
fields, and the **band-assembly rule** (§4.3) is still open. **The row is
therefore `SPEC-READY`, not `DONE`.**

---

## 7. The next evidence, named

1. **A gap-free capture of the running stream** — the only thing that
   settles §4.3. Capture the depth port (10010) with **no dropped segments**
   for ≥ 2 s while the stream runs, so that **≥ 100 consecutive `get_frame`
   replies are contiguous**. Then:
   * take **consecutive** replies and test whether reply *k+1*'s payload
     continues reply *k*'s — the 0xFF invalid-band phase must shift by
     `len(payload) mod 1920` from one band to the next;
   * accumulate bands until the total is a multiple of 1920 and check the
     height is **1200**;
   * read the 0xFF marker and one known column to fix the **pixel-value
     scale** (depth in mm vs a normalised 0–255).
   * Capture the IR port (10013) the same way — **n = 1** here, so the IR
     numbers need their own corpus.
2. **The producer, on disk — DONE (§10).** REA of
   `/Library/Creality/libOrbbecSDK.1.7.1.dylib` (23 MB) settled the array's
   **type tuple and every element's meaning** (§10.3), settled the
   no-data/data "variant" question (§10.4), and **falsified** the premise that
   the serializer holds the band rule (§10.5). `/Library/Creality/RPCServer`
   (230 KB) is **not** the producer — it has no `ob_rpc_*` symbol or string
   (§10.0). The band rule is narrowed to the platform layer / the application
   (§10.7) and is carried by **#132**.
3. **A decoded comparison** — row #126's controlled-content capture (flat
   wall at 300/400/500 mm + checkerboard) with the vendor app as the A/B
   oracle, to turn the 8-bit samples into millimetres.

---

## 10. The producer — the daemon's frame serializer (row #131)

*Added 2026-10-19, row #131 (recon room). This section is the **producer side**
of §2: what the daemon's code actually writes into the 11-element array, and
what it does **not** do. It settles **OQ-124-3** (the meanings of `e2`/`e6`/`e8`
and the "two reply forms") and **narrows OQ-124-2** (the band rule) to a named
place outside the serializer. Every claim below is **`[inferred]`** from static
analysis of the shipped binary — no hardware run in this row, so no `[hw-*]`
tag applies.*

### 10.0 The artifacts, and which one is the producer

| artifact | sha256 | role |
| --- | --- | --- |
| `/Library/Creality/libOrbbecSDK.1.7.1.dylib` (shipped, fat x86_64 + arm64, 23,354,416 B) | `2cf0fcebf2392c7e85ed68f6b8014953eac9988c74703e10e5c8d82747547206` | **the producer** — holds the msgpack-RPC server and all 14 `ob_rpc_*` method names |
| `.recon-cache/libOrbbecSDK_arm64.dylib` (`lipo -thin arm64` of the above; 11,050,032 B) | `5134a211296484473eaa7e4801788943a6ccb6b4a8660465b0a2c042d9dd925b` | the slice actually disassembled and decompiled |
| `/Library/Creality/RPCServer` (shipped, fat, 230,288 B) | `b7f324c0659eed7952473c500e923343486168ad34df7812c0715775cba58fb7` | **not the producer** |

**The work order's premise needs one correction.** `/Library/Creality/RPCServer`
is **not** the daemon that answers `ob_rpc_get_frame`: it contains **zero**
occurrences of `ob_rpc_*` (checked over the whole file), has six exported
symbols (`main`, `getch`, `kbhit`, `handleDeviceConnected`,
`handleDeviceDisconnected`) and only links the Orbbec SDK. It is a small
device-enumeration utility. The msgpack-RPC server the capture talks to lives
**inside `libOrbbecSDK.1.7.1.dylib`**, in `libobsensor::pal::RPCServer`
(namespace `libobsensor`, `pal` = platform-abstraction layer), built on
`clmdep_msgpack2v1` (msgpack-cxx) and the `rpc::` C++ client/server library.
`RPCServer::initVideoStreamingFunction()` is the function that registers the
video-streaming methods.

### 10.1 The method name and the `0-2-10` / `0-2-12` suffix

The registered method name is built as

```
"ob_rpc_get_frame"  +  <SourcePort+0x28 string>  +  to_string(*(uint8*)(SourcePort+0x90))
```

`SourcePort+0x28` holds `"0-2-"` and `SourcePort+0x90` holds the port ordinal
(`10` for depth → 10010, `12` for IR → 10013), giving the wire names
`ob_rpc_get_frame0-2-10` and `ob_rpc_get_frame0-2-12` — **exactly** the method
strings in the capture. The suffix is therefore the **daemon's own port
identity**, not the client's choice. `[inferred]`

### 10.2 `ob_rpc_get_frame` takes **zero** arguments

The handler begins with
`rpc::detail::dispatcher::enforce_arg_count(<name>, 0, <count>)` — it rejects
any request that carries parameters. This confirms #124's observation that
`get_frame`'s request has empty params (`90` = fixarray(0)) as the **daemon's**
contract, not a client convention. `[inferred]`

### 10.3 The 11-element array: the exact type tuple, and every element's meaning

The serializer builds its result with
`clmdep_msgpack::v1::type::define_array<T0,…,T10>::msgpack_object(...)`, and the
SDK's **own client** unpacks the same reply with
`define_array_imp<std::tuple<T0&,…,T10&>,10>::unpack(...)` in
`libobsensor::pal::RPCFrameObject::msgpack_unpack`. The two agree exactly, and
the tuple is:

```
u64, u64, u64, OBFormat, vector<char>, uint, vector<char>, uint, vector<char>, uint, OBIRFrameLightType_t
```

**11 elements — the same 11 the capture sees.** The element-by-element meaning,
read off the producer (`RPCServer::frameCallback` copies these fields out of the
PAL frame) and confirmed by the client's unpack:

| # | wire form (measured) | C++ type | the field it carries |
| --- | --- | --- | --- |
| `e0` | `fixint 0` (11/11) | `u64` | PAL frame word 0 — **always 0**; not a frame index, not an offset |
| `e1` | `uint64` | `u64` | PAL frame word 1 — the frame **timestamp (µs)** |
| `e2` | `uint32` (fixint 0 in 5/10) | `u64` | PAL frame word 2 — a **second time/counter word** of the same frame |
| `e3` | `uint8 255` (11/11) | `OBFormat` (u32) | the frame's **format** field — **not** the stream profile's format id 9 (§10.9, OQ-131-3) |
| `e4` | `bin16`, 12,118–12,276 B | `vector<char>` | the frame's **primary data buffer** — the band |
| `e5` | `uint16` = `len(e4)` | `uint` | the primary data buffer's length |
| `e6` | `bin8`, 19–168 B | `vector<char>` | the frame's **second data buffer** (`data2`) |
| `e7` | `fixint` = `len(e6)` | `uint` | `data2`'s length |
| `e8` | `bin8`, exactly 6 B | `vector<char>` | the frame's **third data buffer** (`data3`) |
| `e9` | `fixint 6` = `len(e8)` | `uint` | `data3`'s length |
| `e10` | `fixint 0` (depth) / `2` (IR) | `OBIRFrameLightType_t` (**int32**) | the frame's **IR light type** — the SDK exposes it as `RPCClientPort::getIRFrameLightType` / `setIRFrameLightType` |

**⇒ OQ-124-3 is answered.** `e6` and `e8` are **not** companions of the frame
and **not** frames: they are the **second and third data planes of the very same
PAL frame** as `e4` — three sibling buffers the platform layer delivers together,
in the same callback, for the same instant. `e8` is 6 bytes because the frame's
`data3` plane is 6 bytes; `e6` is 19–168 B because the frame's `data2` plane
varies. `e2` is the frame's second 64-bit time word (it is 0 in 5/10 replies and
a large value otherwise — it is **not** ASCII, and **not** a frame counter that
increments per band).

The PAL frame's field map, as the producer reads it (offsets are
`libobsensor::pal::VideoFrameObject`):

| PAL frame offset | width | reply element |
| --- | --- | --- |
| `+0x00` | u64 | `e0` |
| `+0x08` | u64 | `e1` |
| `+0x10` | u64 | `e2` |
| `+0x18` | u32 | `e3` (format) |
| `+0x1c` | u32 | `e5` (data length) |
| `+0x20` | ptr | `e4` (data) |
| `+0x28` | u32 | `e7` (data2 length) |
| `+0x30` | ptr | `e6` (data2) |
| `+0x38` | ptr | `e8` (data3) |
| `+0x40` | u8 | `e9` (data3 length) |
| `+0x44` | u32 | `e10` (IR light type) |

### 10.4 The "two reply forms" are **one** form

§2.1 recorded two shapes and §8 OQ 3 left the rule open. There is **no
variant**: the reply is always the same 11-element array. What differs is only
whether the ring buffer held a frame when the call arrived (§10.5).

* **Data form** — the last array word is `{ e9 = 6, e10 = 0 or 2 }`, both small
  positives → msgpack `fixint`.
* **No-data form** — the same array with all three buffers empty (`e4`/`e6`/`e8`
  = `bin(0)`, `e5`/`e7` = 0) and the last word set to
  `{ e9 = <uninitialised>, e10 = 0xFFFFFFFF }`. `0xFFFFFFFF` as an **int32**
  packs as msgpack `int8 -1` (`ff`) — which is exactly the `e10 = -1` #124
  measured. `e3`'s default is `0xFF` (255) in **both** forms.

  **A real defect the client must know about:** the producer writes **only the
  high half** of that last 64-bit word (`e10`) and never initialises the low
  half (`e9`). So **`e9` in the no-data form is uninitialised stack** — the
  capture's `0xA2C90018` is a leftover, not a value. `[inferred]`
  ⇒ **A decoder must branch on "are the buffers empty", never on the wire types,
  and must ignore `e9` when `e8` is empty.** The SDK's own client does exactly
  this: it pre-initialises its `RPCFrameObject` to `{e0..e2 = 0, e3 = 255,
  e4..e9 = empty/0, e10 = -1}` before unpacking.

### 10.5 The serializer does **not** split the frame — it is a pass-through

This is the row's main negative result, and it falsifies the premise that the
band rule lives in the serializer.

* `RPCServer::frameCallback(VideoFrameObject const&)` is the producer's only
  input. It allocates one **128-byte record** and copies the PAL frame's three
  buffers into it **whole**: `memcpy(dst, src, size)` with `size` taken from the
  frame's own `dataSize`/`data2Size`/`data3Size` fields. **There is no slicing,
  no `min()`, no row arithmetic, no 12,288-byte constant anywhere in it.**
* Records are pushed onto a **ring buffer of 256 entries per page**, pages
  chained: the head counter lives at `server+0xf8`, the live count at
  `server+0x100`, the page pointer at `server+0xe0`, and a page is released when
  the counter passes `0x1FF`.
* `ob_rpc_get_frame` **pops one whole record per call** and serialises it. It
  never splits either. The payload length it writes is therefore **the PAL
  frame's primary data buffer's length, verbatim**.

⇒ **The ~12 KB band is not produced by the serializer.** The serializer's
`bin` length is whatever the platform layer handed it. `[inferred]`

### 10.6 The SDK's own RPC client does **not** stitch bands either

`RPCClientPort::startStream` spawns a thread whose body is a loop that calls
`ayncCall<RPCFrameObject>("ob_rpc_get_frame")` and then **hands the payload to
the application callback unchanged**: the `VideoFrameObject` it builds has its
`data` pointer = the received bin's begin, its `data` end = the bin's end, and
its `dataSize` = element 5. **No concatenation, no offset arithmetic.** When the
call returns no frame it sleeps `1000 / profile.fps` ms and retries.

⇒ The **stitcher is not in the Orbbec SDK either**. Whatever reassembles the
12 KB bands into a 1920×1200 image lives in the **CrealityScan application**
(`GameAssembly.dylib`, IL2CPP) or in the platform layer that produced the
banded PAL frame. `[inferred]`

### 10.7 Where the band rule therefore must be — the named next reads

Given §10.5 and §10.6, the rule is in one of exactly two places, both
nameable:

1. **The platform-layer frame producer** — the thing that decides the PAL
   frame's `dataSize` (~12 KB instead of 2,304,000 B). Named reads in the same
   dylib: `libobsensor::VideoSensor::onFrameRawDataCallback` (`0x369d68`),
   `libobsensor::OpenNIVideoSensor::onNewFrameDataCallback` (`0x37ded0`),
   `libobsensor::FrameBufferManager<libobsensor::FrameSet>::acquireFrame`
   (`0x3a1fe0`), `libobsensor::AbstractFrameBufferManager::getFrameDataBufferSize`
   (`0x398930`), `libobsensor::OpenNIFrameBufferManager::Init` (`0x3a2f98`).
   A frame-buffer manager that assembles a frame from pieces is the most likely
   home of a "how many bytes are ready" rule. `[inferred]`
2. **The CrealityScan application** — the IL2CPP side that stitches the bands
   (the SDK client passes them through untouched). The secondary confirmation
   route named in the room's method.

### 10.8 What this does to the OQ list

* **OQ-124-3 — settled.** §10.3 names `e2`, `e6`, `e8` (and every other
  element); §10.4 settles the "two forms" question. Neither was a separate
  mystery: all four fields are ordinary fields of one PAL frame.
* **OQ-124-2 — narrowed, not settled.** The rule is **not** in the serializer
  (§10.5) and **not** in the SDK's RPC client (§10.6). What is now known is
  the rule's *shape*: the payload is a **contiguous slice of the frame's primary
  data buffer**, delivered whole; a band boundary is a property of the platform
  layer or of the device transport, and the client-side stitch rule is outside
  the SDK. The remaining evidence is named in §10.7 and carried by **#132**.

### 10.9 OPEN QUESTIONS raised by this row

1. **OQ-131-1 — where exactly is the band cut?** Not in the serializer
   (§10.5), not in the SDK client (§10.6). Named next reads in §10.7; the
   runtime confirmation belongs to **#132**.
2. **OQ-131-2 — is the band aligned to a row?** The measured payloads are
   6 full rows + a partial row of 598–756 B, and the 0xFF invalid marker
   recurs every 1920 B *inside* a payload. Whether the band *starts* on a row
   boundary cannot be decided from a single band. **#132** must compare the
   0xFF phase of consecutive bands.
3. **OQ-131-3 — what is `e3` = 255?** It is an `OBFormat`-typed field taken
   from the PAL frame's `+0x18`, always 255 on the wire, and it is **not** the
   stream profile's format id 9 (`protocol-spec.md` §7). The SDK's enum names
   for 255 are not present in the shipped dylib, so "unknown" vs "custom"
   cannot be resolved from the binary. It is, however, now **known to be the
   frame's format field** rather than a status byte — which is what §6's
   acceptance clause (2) was missing.
4. **OQ-131-4 — what is `e2` exactly?** It is the PAL frame's word at `+0x10`,
   a second 64-bit time word, 0 in 5/10 depth replies and a large value
   otherwise. Its unit and role are not named by the binary. It is **not** a
   band index and **not** a frame ordinal (it does not increment per band in
   the corpus).

---

## 8. OPEN QUESTIONS

1. **What is the band-assembly rule? — NARROWED, still open.** Each reply
   carries 6 whole rows plus a partial row of a 1920-wide image. §10.5 proves
   the **serializer does not split** (it copies the platform frame's buffers
   whole) and §10.6 proves the **SDK's RPC client does not stitch** (it hands
   the bin through unchanged). The rule is therefore in the platform layer that
   produced the ~12 KB frame buffer, or in the CrealityScan application that
   stitches the bands. **Next evidence: §10.7 (named reads) + §7.1 and
   #132.** (This is OQ-124-2.)
2. **What are `e6` (19–168 B, pixel-like) and `e8` (6 B, `ff ff ff ff ff ff`
   on depth, `b1 1b 00 00 99 03` on IR)? — SETTLED (§10.3).** They are the
   **second and third data planes of the same platform frame** as `e4` — the
   frame's `data2` and `data3` buffers, delivered together in the same
   callback. `e8`'s 6 bytes are the frame's `data3` length; `e6` is the
   frame's `data2` length. Neither is a frame, and neither is a "companion" in
   any sense beyond "another buffer of the same frame".
   (This is OQ-124-3.)
3. **Why does the reply have two shapes? — SETTLED (§10.4).** It does not.
   There is **one** 11-element array; the "no-data" form is the same array
   with empty buffers and `e10 = -1` (the producer writes `0xFFFFFFFF` into
   the high half of the last word, which msgpack packs as `int8 -1`), and it
   **never initialises `e9`** — so `e9`'s value in that form is uninitialised
   stack and must be ignored. A decoder branches on "are the buffers empty",
   not on the wire types.
4. **What is `e2`? — PARTLY SETTLED (§10.3, OQ-131-4).** It is the platform
   frame's word at `+0x10`: a **second 64-bit time word** of the same frame.
   It is **not** ASCII, **not** a band index and **not** a per-band counter.
   Its unit and role are not named by the binary.
5. **What does one pixel byte mean?** 8-bit single-channel is settled; the
   *scale* (depth in mm? a normalised 0–255? a log encoding?) is not.
   **Next evidence: §7.3.**
6. **Is the device's format id 9 the SDK's `OB_FORMAT_Y8`?** The payload's
   8-bit single-channel layout is consistent with it, but the ids may be the
   device's own (§1.3 of the old plan, unchanged). The frame reply carries
   no format field, so the corpus cannot settle it.
7. **Why are 5,875 of 5,886 `get_frame` replies empty?** The client polls at
   ~85 Hz and the device answers with data in 11 of them within the capture's
   93 %-missing window. Whether that is the device's rate, the capture's
   loss, or the client's poll pattern is **not** settled.

---

## 9. Corrections to the repo's existing records

These supersede the older text; the newest word wins.

* **`protocol-spec.md` §1.3** — "the response is `[1, msgid, nil, bin(N)]`" is
  **wrong**: the result is an **11-element array** and the frame payload is
  its **element 4** (§2.1). The same section's "`9b 00 cf 00` =
  `bin8(52,992)`" is invalid msgpack (`cf` is uint64; 52,992 = 0xCF00 is a
  big-endian *read* of the byte pair) — already flagged in §1.2 of the old
  plan, and still invalid.
* **The old §1.2 of this file** — the one recorded 24-byte head
  (`940105c0 9b 00 00 00 cc ff c4 00 00 c4 00 00 c4 00 ce a2 c9 06 18 ff`)
  is the **no-data form**: all three buffers empty. It is a *whole*
  `get_frame` reply, but it carries **no frame** — so it could never have
  shown the payload's first bytes. The "which slot holds the payload"
  question is answered by this run: **slot 4**, 11/11.
* **`protocol-spec.md` §1.3's "10–53 KB"** — the upper end is **not
  corroborated** by this corpus: the 11 payload-bearing replies are
  12,118–12,276 B, and the **maximum** length the daemon declares anywhere
  in the capture is 12,276 B (§4.2). The 53 KB figure needs its own
  provenance before it is used again.
* **The old §1.1/§1.3's "≈162k per port counts segments, frames are bounded
  at ~7.4k/interface"** — consistent with what is measured: 7,260 depth /
  7,420 IR `get_frame` **requests** in the capture, and 5,886 replies whose
  shape is a `get_frame` result.
