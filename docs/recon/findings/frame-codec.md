# Frame container identification — what is inside the `get_frame` bin? (row #124)

*Status: **MEASURED — the container is named.** The plan's T1–T6 were run on a
corpus of **n = 11** complete `get_frame` replies (**10 depth, 1 IR**) extracted
from `session4.pcapng` with the fixed extractor (§1). The container is the
**msgpack-RPC reply envelope**; the frame payload it carries is
**headerless raw 8-bit image samples** (row stride **1920 B**).*

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
2. **The producer, on disk** — REA `/Library/Creality/RPCServer` (230 KB) and
   `/Library/Creality/libOrbbecSDK.1.7.1.dylib` (23 MB) for the **frame
   serializer**: the function that writes the 11-element array and the
   band-splitting rule. This settles §4.3, the meaning of `e2`/`e3`/`e6`/`e8`
   and the no-data/data variant outright, and is cheaper than a capture.
3. **A decoded comparison** — row #126's controlled-content capture (flat
   wall at 300/400/500 mm + checkerboard) with the vendor app as the A/B
   oracle, to turn the 8-bit samples into millimetres.

---

## 8. OPEN QUESTIONS

1. **What is the band-assembly rule?** Each reply carries 6 whole rows plus a
   partial row of a 1920-wide image; the rule that stitches bands into a
   1920×1200 frame is not in this corpus (§4.3). **Next evidence: §7.1.**
2. **What are `e6` (19–168 B, pixel-like) and `e8` (6 B, `ff ff ff ff ff ff`
   on depth, `b1 1b 00 00 99 03` on IR)?** `e6` is the same kind of data as
   the payload at 1/400th the size; `e8` is 3 × `u16` with `0xFFFF` as
   "absent" on depth. Neither is a frame; neither is understood.
   **Next evidence: §7.2.**
3. **Why does the reply have two shapes?** In the no-data form `e9`/`e10` are
   `uint32` and `int8 −1`; in the data form they are `fixint 6` and
   `fixint 0/2`. A decoder must branch on the form; the rule is unknown.
4. **What is `e2`?** A `uint32` that is nonzero in 6/11 replies and whose
   nonzero values are large and ASCII-looking (0x29252827, 0x32303138 =
   `"2018"`). Not a frame index as read; not understood.
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
