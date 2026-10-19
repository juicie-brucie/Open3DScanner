# Frame container identification — what is inside the `get_frame` bin? (row #124)

*Status: **PLAN ONLY.** The corpus (row #123's `tools/frame_bins` output) is not
available yet, so **no test in §2 has been run** and this file contains no
measurement. Every claim below is `[inferred]` — derived from the repo's own
records and from the public MIT OrbbecSDK v2 headers. No capture was parsed and
no vendor binary was read for this file.*

*Wall: `docs/recon/**`. This row does not touch
`engine/include/openscanner/obscan_api.h` — the frame container is a **wire**
format, not part of the recovered C ABI.*

---

## 0. The question

`ob_rpc_get_frame` answers with a msgpack payload of 10–53 KB for a
1920×1200 stream (`protocol-spec.md` §1.3). Raw 8-bit pixels would be
2,304,000 B, so the payload is *encoded* — but **what** it is, is not settled.
Row #124 asks for the container's identity: is it a standard codec (so a
standard decoder can be named), or a vendor container (so the vendor's own
decode path becomes the reference)?

This file is the **identification plan**: the tests, what each outcome implies,
what would falsify each hypothesis, and the acceptance that lets the row leave
`BLOCKED`. When the extractor lands, the numbers replace the plan's "expected"
columns and the hypothesis table is resolved.

---

## 1. Pre-flight — what the repo already holds

### 1.1 The frame response

| fact | source | tag |
| --- | --- | --- |
| response is `[1, msgid, nil, result]`; the result is the frame | `protocol-spec.md` §1.1, §1.3 | `[inferred]` |
| bins are **10–53 KB** for a 1920×1200 stream → not raw pixels; "the codec is **not settled**" | `protocol-spec.md` §1.3 | `[inferred]` |
| IR = port 10013 (suffix `0-2-12`), depth = port 10010 (suffix `0-2-10`) | `protocol-spec.md` §1.1 | `[inferred]` |
| the only profile ever streamed, on **both** interfaces: `[streamtype, **9**, 1920, 1200, 50]` | `protocol-spec.md` §7; `stream-profiles-row114.md` §4 | `[inferred]` |
| profile-list format ids observed across the three interfaces: **{0, 5, 9, 24}** | `stream-profiles-row114.md` §1 | `[inferred]` |
| the `format` field is "a device-side pixel/codec id, not on the wire as a name" | `stream-profiles-row114.md` §1, §8 item 4 | `[inferred]` |
| ~7,420 IR / ~7,260 depth `get_frame` calls in the 86 s window, vs 162,127 / 167,749 **segments** on those ports | `protocol-spec.md` §1.1, §1.3 | `[inferred]` |
| the ports also carry ~146k/147k **208-byte** segments whose role is open | `protocol-spec.md` §1.3, §6 item 2 | `[inferred]` |

Two consequences for the plan:

* The corpus must be selected **by method, not by port traffic**. If the
  extractor reports ~162k frames per port, it is counting segments, not
  frames — the `get_frame` call count bounds the corpus at ~7.4k/interface.
  That reconciliation is step 0's job (§2.1).
* The **208-byte segments are not frames** and must not enter the corpus.

### 1.2 The one recorded frame head — and a discrepancy

`streaming-replay-payloads.md` §5 carries the repo's only frame-response head
(the first IR `get_frame` answer, msgid 5, t=25.339836):

```
940105c09b000000ccffc40000c40000c400cea2c90618ff...
```

A msgpack parse of those 24 bytes gives `[1, 5, nil, **fixarray(11)**]` whose
elements begin `0, 0, 0, uint16(65476), 0, 0, bin8[0], 0, bin8[0], uint64…` —
and the 24 recorded bytes **end inside element 9** (a `uint64` whose 8 payload
bytes are cut after 5). So the repo holds a *prefix*, not a message. `[inferred]`

**Discrepancy to settle, not to inherit:** `protocol-spec.md` §1.3 says the
response is `[1, msgid, nil, bin(N)]` and glosses the observed head `9b 00 cf 00`
as "`bin8(52,992)`". Under the msgpack grammar `9b` is *fixarray(11)* and `cf`
is *int8*; 52,992 = 0xCF00 is a big-endian 16-bit **read** of the byte pair
`cf 00`, not a msgpack length. The two repo records of the frame head
therefore disagree with each other and with the grammar: one says "bin",
the other says "array of 11". `[inferred]`

Nothing here is settled by argument — it is settled by the extractor's bytes
(step 0, §2.1). But it changes what the container even *is*: the first bytes
of the "bin" may be **msgpack**, not an image magic, and the image magic may
sit at an offset inside a struct.

### 1.3 The format field — a candidate prior (not a mapping)

The public MIT OrbbecSDK v2.10.6 header
(`include/libobsensor/h/ObTypes.h`, from the on-disk SDK tarball) defines:

```c
OB_FORMAT_YUYV = 0, OB_FORMAT_MJPG = 5, OB_FORMAT_Y16 = 8, OB_FORMAT_Y8 = 9,
OB_FORMAT_RLE = 21, OB_FORMAT_COMPRESSED = 26, OB_FORMAT_RVL = 27,
OB_FORMAT_Y14 = 24, …
```

The device's observed id set **{0, 5, 9, 24}** is a subset of that enum, and
the streamed id is **9**. If the device numbers formats like the SDK, then the
streamed profile is **Y8** — 8-bit single-channel, 2,304,000 B raw at
1920×1200 — which puts the wire's 10–53 KB at a **43×–230×** reduction and
says the daemon compresses a format that names an *uncompressed* pixel layout.
If the numbering is the device's own, the mapping is unknown and the container
test stands alone. `[inferred]`

**This is a prior, not a finding.** It is used only to (a) order the decoder
trials — MJPEG first, because `OB_FORMAT_MJPG` exists and the SDK bundles
`3rdparty/libjpeg` (turbojpeg) and `3rdparty/libuvc/src/src/frame-mjpeg.c`;
and (b) supply a *known constant* for the structure scan (§2.6). Its
falsification is explicit: if format 9 were a compressed format the SDK's enum
says is uncompressed, the header's format field will not read 9, and the
device's ids are not the SDK's.

Note the SDK v2 is a **different codebase that does not list this device**
(`FULL-INDEPENDENCE.md` §L2), so its enum is a *reference*, never an authority
for this device's ids.

### 1.4 What the corpus will be, and the contract questions

Row #123's extractor writes to an output directory outside the repo. Its
in-flight output directory already exists with the index header:

```
port  sequence  timestamp  byte_length  method  source  file
```

and a sibling run shows the naming convention `<interface>/NNNNNN.bin`. **The
contract questions that must be answered when the extractor lands** (they are
inputs to the tests, not separate work):

1. **What does a `.bin` contain** — the RPC *result* (which §1.2 shows may be
   a msgpack array, not a bin) or the *inner payload* the result carries? The
   magic scan must read the **payload's own first bytes**, so this must be
   stated in the extractor's output documentation.
2. **What `byte_length` counts** — the payload length, or the on-wire length.
3. **Per-interface separation** — the corpus must be `ir` (10013) and `depth`
   (10010) separately; mixed ports are how the replay tooling went wrong
   (row #120).
4. **The per-port counts** — frames, not segments (§1.1).

### 1.5 What is already on disk for the "next evidence"

* `~/DevReposiMac/OrbbecSDK_v2.10.6/` — the MIT SDK (headers + binaries +
  `src-read/`, a partial read of the source with the full `tree.txt`).
* `/Library/Creality/RPCServer` — the daemon that **produces** these bins
  (world-readable, 230 KB).
* `/Library/Creality/libOrbbecSDK.1.7.1.dylib` (23 MB) and
  `/Applications/CrealityScan.app/Contents/PlugIns/` — the vendor's own SDK,
  which **consumes** them.

---

## 2. The tests

Every test runs **per interface** (IR 10013, depth 10010) and reports the
numbers verbatim. The corpus is sampled in **file order** (= capture order) so
consecutive-frame tests (T5) are meaningful.

### 2.1 Step 0 — settle the shape of the answer (before any codec test)

* Re-parse the first N (≥50) frame messages with a msgpack reader and record
  the **result element's** shape: `bin` / `str` / `fixarray(n)` / `map`, the
  array's arity, and the type of each element.
* Record the msgpack **header bytes of the payload** (the bytes that precede
  the image data, if any) and their offsets.
* Reconcile the corpus size with the ~7.4k/interface `get_frame` count.

**Implication.** If the result is a bare `bin`, the payload's first bytes are
the container's first bytes and T1 runs at offset 0. If the result is an
array (as the one recorded head suggests), the payload is at some **offset**
inside it, and T1/T2 must be re-run at that offset — *and* the array's own
layout becomes a second deliverable (it is the frame *envelope*).

### 2.2 T1 — magic / first-bytes scan

* For every bin: the first 16 bytes; the first 4; the first 8.
* Cluster the corpus by the first 4 bytes → **how many distinct heads**?
  1 distinct = fixed magic; many = the head carries a varying field
  (length/timestamp/sequence).
* Compare the head against the standard signatures:

| family | signature |
| --- | --- |
| JPEG | `FF D8 FF` (SOI + marker) |
| PNG | `89 50 4E 47 0D 0A 1A 0A` |
| zlib | `78 01` / `78 9C` / `78 DA` (+ `78 5E`) |
| gzip | `1F 8B` |
| raw deflate | (no signature — trial by decode) |
| HEVC / HEIF (ISO-BMFF) | `… 66 74 79 70` + brand `heic`/`heix`/`hevc`/`hvc1`/`mif1`/`msf1` |
| HEVC / H.264 Annex-B | `00 00 00 01` / `00 00 01` + NAL type |
| LZ4 frame | `04 22 4D 18` |
| Zstd | `28 B5 2F FD` |
| LZMA / xz | `5D 00 00` / `FD 37 7A 58 5A 00` |
| BMP / TIFF | `42 4D` / `49 49 2A 00` / `4D 4D 00 2A` |
| RLE-ish / vendor | `OB_FORMAT_RLE`/`RVL` families — no published signature; trial by structure |

**Implication.** A standard signature in ≥99% of the corpus ⇒ **H1** (standard
container) and the decoder is named from the trial that succeeds. No signature
⇒ **H2** (vendor container) or **H3** (sparse point data); the head is then
characterised field-by-field by T5/T6.

### 2.3 T2 — standard-decoder trials

For each candidate in T1's list (and MJPEG first, per §1.3), run the decoder
on the corpus and record **the decode rate**, not a pass/fail:

* JPEG: `libjpeg`/turbojpeg (in the SDK tarball), or `sips`/`ffmpeg`; report
  how many bins decode, the decoded dimensions, and whether the decode
  consumes the whole bin.
* PNG: `libpng`/`sips`; same report.
* zlib/gzip/deflate: `zlib` with wbits 15 / 31 / −15; report the inflated
  length vs the expected 2,304,000 B.
* HEVC/HEIF: `ffmpeg`/`heifdec`; report the frame count and dimensions.
* LZ4 / Zstd / LZMA: `lz4`/`zstd`/`xz`; report the decompressed length.

**Implication.** A decoder that succeeds on ≥99% of the corpus **and** yields a
1920×1200 image ⇒ the container is that codec, named. A decoder that succeeds
on a *few* bins (e.g. 3/5000) ⇒ false positive, not a codec. A decoder that
succeeds on all bins but yields the wrong dimensions ⇒ a headered wrapper
around that codec (name it as such: "container = X + header").

### 2.4 T3 — entropy

Per interface, over the whole corpus and per frame:

* Shannon entropy of the byte distribution (bits/byte) — mean, p5, p95.
* The **byte histogram shape**: uniform (compressed/encrypted), smooth
  (raw image), heavy zero mass (depth with invalid pixels).
* The fraction of zero bytes and of 0xFF bytes.

**Implication.** ≥7.9 bits/byte ⇒ the payload is compressed or obfuscated;
≤7.0 with a smooth histogram ⇒ uncompressed image data (contradicts the
10–53 KB observation — say so); a spiky histogram with a large zero mass ⇒
sparse/quantized data (H3).

### 2.5 T4 — size vs content

The corpus has **no known scene**, so content is proxied:

1. **Size vs. time**: the size series over the capture. A static scene gives
   a near-constant series; a moving one gives a spread. Report min/p50/p95/max
   and the coefficient of variation, per interface.
2. **Size vs. the profile**: the ratio to 1920×1200×1 B (2,304,000) and to
   1920×1200×2 B (4,608,000) — the compression ratio under each hypothesis.
3. **Size distribution shape**: unimodal (one stream) or bimodal (two kinds
   of frame mixed into one port — a corpus-pollution check).
4. **Temporal byte-difference rate**: the fraction of bytes that differ
   between consecutive frames. This is the decisive raw-vs-compressed
   discriminator without a known scene: a raw image of a static scene differs
   in a few percent of bytes; a *compressed* stream differs in ~100% of bytes
   even when the scene is static.
5. **Stride check (H3)**: if the payload is point data, `length mod stride`
   must be constant for a plausible stride (8/12/16/24/28/32), and the
   implied point count must be plausible for 2.3M pixels.

**Implication.** A stable size + low temporal byte-difference rate ⇒ the
payload is *not* compressed per frame (it is raw or lightly coded) — which
would contradict the 10–53 KB observation and must be reported as a
contradiction, not smoothed over. A near-100% temporal byte-difference rate
with a stable size ⇒ a fixed-rate codec (video-style inter-frame coding) —
**H1-video**.

### 2.6 T5 — header sharing between consecutive frames

* The longest common prefix (LCP) between consecutive bins: the distribution
  over the corpus (min/p50/max), per interface.
* The fraction of bins sharing the first 4 / 8 / 16 bytes with the previous
  bin, and the fraction sharing them with the corpus-wide mode.
* If the LCP is a constant length K ≥ 8, the first K bytes are a **per-frame
  header** — and its varying fields are found by diffing headers of
  consecutive bins (which bytes change, and how they correlate with the bin's
  length and timestamp).

**Implication.** A constant K ⇒ there *is* a header, and T6 can read it. A
constant K of 0–2 ⇒ no header, the payload is monolithic, and the container
(if any) is a pure codec — which points back to T2.

### 2.7 T6 — self-describing structure scan

Two scans that use what is *known* rather than guessed:

1. **Known-constant scan**: the streamed profile's constants — width **1920**
   (0x0780), height **1200** (0x04B0), fps **50** (0x32), format **9** — as
   u16/u32, LE and BE, at every offset in the first 64 bytes. If two or more
   of them co-occur at a **constant offset** across ≥99% of the corpus, the
   header is identified and its layout is read off directly.
2. **Self-length scan**: at every offset in the first 64 bytes, look for a
   u16/u32 (LE/BE) whose value equals the bin's remaining length (or its
   total length). A constant offset where it holds ⇒ a length-prefixed
   container, and the payload begins at the next field — **re-run T1/T2 at
   that offset.**

**Implication.** A hit is a header layout, directly satisfying acceptance
§5.1. No hit on either scan ⇒ the payload does not describe itself; the
container is then whatever T2 names, or the payload is opaque (H2/H5).

---

## 3. The decision table (test outcome → conclusion)

| T1 head | T2 decoder | T3 entropy | T4 temporal diff | Conclusion |
| --- | --- | --- | --- | --- |
| standard signature | succeeds ≥99% | ≥7.9 | ~100% | **H1 standard codec** — name the decoder |
| standard signature | succeeds ≥99% | — | — | **H1 + prefix** — name the codec *and* the wrapper |
| no signature | none | ≥7.9 | ~100% | **H2 vendor container** (or H5 obfuscated) → §6 |
| no signature | none | ≥7.9 | low | **H2/H3** — payload is compressed but not per-frame; run T5/T6 |
| no signature | none | 5–7, smooth | low | **H4 raw-ish** — contradicts the size bound; report the contradiction |
| no signature | none | spiky, many zeros | — | **H3 sparse/quantized** → run the stride check |
| n/a | n/a | ~8.0 uniform | size independent of motion | **H5** — not compressible image data; vendor-proprietary or encrypted |

---

## 4. Falsification — what would kill each hypothesis

| Hypothesis | Falsified by |
| --- | --- |
| **H1 — standard codec** (JPEG / PNG / zlib / HEVC / LZ4 / Zstd / …) | T1 finds no standard signature at offset 0 **and** at the offset step 0/T6 identifies; **and** T2's best decoder succeeds on <99% of the corpus (a handful of successes is a false positive, not a codec); **and** T3 shows entropy <7.5 bits/byte (not compressed) |
| **H2 — vendor container** (proprietary header + body) | a standard decoder succeeds on ≥99% of the corpus unmodified (⇒ it is H1, not H2); or the bytes are an uncompressed image (⇒ H4, and the size bound is wrong); or T6 finds a self-length field with no standard magic anywhere (⇒ still H2, but a *known-layout* H2 — the acceptance is then met on layout alone) |
| **H3 — sparse point / measurement data** (points, not pixels) | `length mod stride` is not constant for any plausible stride; or the implied point count is implausible for 2.3M pixels; or the values do not behave like float32 coordinates (magnitude/entropy profile) |
| **H4 — uncompressed raw pixels** | the bin length equals width×height×bytes-per-pixel for the profile (2,304,000 for 8-bit, 4,608,000 for 16-bit). **Already excluded** by the 10–53 KB observation; T4 re-confirms it stays excluded |
| **H5 — obfuscated/encrypted payload** | T5 finds a constant header (⇒ there is structure, so not opaque); or T6 finds self-describing fields; or the size correlates with scene motion (⇒ compressible content) |

A hypothesis is only **kept** if it survives its own falsification row. If
more than one survives, the row is *not* identified — the file says so and
names what would separate them.

---

## 5. Acceptance — what "the container is identified" means

> **ACCEPTANCE:** *The container is identified* **iff**, per interface, (1) one
> container is named — a standard codec **with a named decoder that succeeds
> on ≥99% of the corpus**, or a vendor container **with a byte-exact header
> layout** (magic + fields + widths + endianness + the payload's offset) that
> holds on **≥99% of the corpus** with **zero** exceptions in the
> declared-length check; (2) the header's width/height/format agree with the
> streamed profile (1920×1200, format 9) on ≥99% of the corpus; (3) the byte
> accounting closes — header + payload = bin length, no unexplained residue —
> on ≥99% of the corpus; and (4) if vendor-specific, the **specific** next
> evidence is named (§6). *Otherwise the container is **not identified**: the
> row stays open and the file says which of (1)–(4) failed.*

IR and depth are reported **separately**; if they share a container, that is
stated with its evidence (they usually differ — depth is the one that would
carry a second, wider channel).

---

## 6. If it is vendor-specific — the named next evidence

Ranked, with the exact artifact:

1. **The producer, on disk**: `/Library/Creality/RPCServer` (the daemon that
   *creates* these bins) and `/Library/Creality/libOrbbecSDK.1.7.1.dylib` —
   REA them for the frame **serializer** (the function that turns a device
   frame into the RPC payload). The serializer is the mirror of the
   identifier, and the daemon is only 230 KB.
2. **The consumer's reference**: the public MIT SDK's frame path —
   `src/core/frame/Frame.cpp`, `src/core/stream/StreamProfile.cpp`,
   `include/libobsensor/h/ObTypes.h` (the format enum, §1.3) and
   `3rdparty/libjpeg` (turbojpeg) for an MJPEG path. The v2 SDK does not list
   this device (`FULL-INDEPENDENCE.md` §L2), so this is a *mechanism*
   reference, not an authority.
3. **A decoded comparison**: row #126's controlled-content capture (flat wall
   at 300/400/500 mm + checkerboard), with the vendor app as the A/B oracle —
   the only way to prove a decode is *correct*, as opposed to *plausible*.

---

## 7. OPEN QUESTIONS

1. **What is the frame result element?** The repo's two records disagree: a
   `bin` (`protocol-spec.md` §1.3) vs a **fixarray(11)** whose 24 recorded
   bytes truncate mid-element (`streaming-replay-payloads.md` §5). Step 0
   settles it; until then, "the payload's first bytes" is not a defined
   offset. **Carries to #122**: the client's "the inner bin at index 3"
   assumption is only valid if the result *is* a bin.
2. **Are the device's format ids the SDK's `OB_FORMAT_*` values?** The
   observed set {0, 5, 9, 24} is a subset of the SDK enum and 9 would be
   Y8 (uncompressed 8-bit) — which the 10–53 KB size forbids. Either the ids
   are device-specific, or the daemon compresses a format named "uncompressed".
   Falsified by the header's format field not reading 9, or by the format
   field being absent.
3. **What does "≈162k per port" count** in row #123 — segments or frames? The
   `get_frame` call count bounds frames at ~7.4k/interface.
4. **Are the 208-byte segments** on the frame ports a second sub-stream, a
   preview, or a segmentation artifact? (`protocol-spec.md` §6 item 2.) They
   must be excluded from the corpus; if they carry pixels, the "container"
   question has two answers.
5. **Pull semantics** — a fresh frame or the latest re-sent? (row #104 /
   `protocol-spec.md` §6 item 3.) Affects T4's temporal statistics: if the
   daemon re-sends the latest frame, consecutive bins may be *identical*, and
   the temporal byte-difference rate would read ~0 on a moving scene too.
   T4 must therefore report the **fraction of byte-identical consecutive
   pairs** alongside the difference rate.
6. **Does the container carry the profile's own fields** (width/height/
   format/fps)? T6's known-constant scan answers it; if no field matches, the
   container is content-agnostic and the dimensions must come from the
   profile, not the payload.
