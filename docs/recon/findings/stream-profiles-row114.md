# Stream Profiles per Interface — Row #114

*Observation-derived from the 2026-10-18 capture set (`session4/small_payloads.tsv`,
354,430 lines, plus `crealityscan-session_00023…pcapng` for the ring tail). All
claims `[inferred]` — capture-derived only; no gated run, no vendor binary read
except the §6 cross-check, which is a negative-result string search. The
captures stay outside the repo (`~/DevReposiMac/raptor-protocol-capture/`).*

## 0. The question and the answer

Row #114 asks: does a 60 fps profile exist, and which profile does each
scanning mode use?

- **Yes — 60 fps profiles exist on IR and depth** (§2). So do 50 fps profiles
  at 1920×1200.
- **Which profile each scanning mode (22-line blue / 7-line blue / NIR)
  selects is NOT on the wire** (§8). The device keeps a mode table in flash
  (structure 4035, §3) that names the three (w, h, fps) rows, but nothing in
  the capture maps a *mode name* to a *row*. That needs a new capture; §8
  gives the recipe.

## 1. `ob_get_stream_profile_list` is issued on all three interfaces

The prior session's claim (one call, RGB only) was wrong: the client asks
**each interface** for its own list. Three requests, three responses, all in
session4:

| interface | port | suffix | request t | response t | resp bytes | entries |
|---|---|---|---|---|---|---|
| RGB | 10012 | `0-2-14` | 25.152891 | 25.154259 | 237 | **23** |
| IR | 10013 | `0-2-12` | 25.263603 | 25.263825 | 547 | **54** |
| depth | 10010 | `0-2-10` | 25.264037 | 25.264224 | 898 | **89** |

Request wire form (all three): `9400 <msgid> d9 20 "ob_get_stream_profile_list<suffix>" 90`
— msgpack-RPC `[0, msgid, method, []]`. Response wire form:
`9401 <msgid> c0 <array>` — `[1, msgid, nil, <entries>]`, i.e. the result is
the array directly (not nested under a key). `[inferred]`

**Entry shape**: `[0, format, width, height, fps]` — five ints, first always
`0`. This is the same shape as `ob_rpc_start_stream`'s parameter array minus
its leading stream-type element: `start_stream` sends
`[streamtype, format, width, height, fps]`, i.e. **profile params 2–5 with
the stream type prepended**. The lists' entries therefore name the *same*
triples the start call uses. `[inferred]`

`format` is a device-side pixel/codec id, not on the wire as a name; observed
values in these three lists: **0, 5, 9, 24**. (The first element of the
`start_stream` array is a different thing — the stream type: 6 = IR, 7 = depth,
§4.)

### 1.1 The three lists, summarized

| | RGB (23) | IR (54) | depth (89) |
|---|---|---|---|
| formats | 0 (×10), 5 (×13) | 9 (×34), 5 (×20) | 9 (×39), 5 (×30), 24 (×20) |
| resolutions | 1280×800, 640×400 | 960×600, 1280×800, 1520×1200, 1600×1000, 1920×1200 | 480×300, 960×600, 1280×800, 1520×1200, 1600×1000, 1920×1200 |
| fps present | 5,10,15,20,25,30,50,60 | 5,10,15,20,25,30,40,50,60,90 | 5,10,15,20,25,30,40,45,50,60,90 |

RGB max is 1280×800@60 (format 5) — RGB never offers 1920×1200. IR and depth
both offer 1920×1200. `[inferred]`

## 2. 60 fps profiles, and the per-resolution ceilings

**60 fps exists on both IR and depth** — this is the row's headline answer.

- **IR: 6 entries at 60 fps** (four distinct resolutions; 1280×800 and
  1520×1200 appear twice, once per format):
  `[0,9,960,600,60]`, `[0,9,1280,800,60]`, `[0,9,1520,1200,60]`,
  `[0,9,1600,1000,60]`, `[0,5,1280,800,60]`, `[0,5,1520,1200,60]`.
- **Depth: 11 entries at 60 fps**:
  `[0,24,1520,1200,60]`, `[0,24,1920,1200,60]`, `[0,9,960,600,60]`,
  `[0,9,1280,800,60]`, `[0,9,1520,1200,60]`, `[0,9,1600,1000,60]`,
  `[0,24,960,600,60]`, `[0,5,1280,800,60]`, `[0,5,1520,1200,60]`,
  `[0,5,1600,1000,60]`, `[0,5,960,600,60]`.

> **Correction to the prior session's draft**: it recorded "depth 12 entries at
> 60 fps" and "IR 8 (960×600, 1280×800, 1520×1200, 1600×1000)". Recount from
> the decoded lists: depth has **11** at 60 fps, IR has **6** (over the four
> resolutions it names). The resolution list was right; the counts were not.

**Ceilings** `[inferred]`:

- **Maximum fps offered anywhere is 90.** IR: 90 fps occurs only at 960×600
  (formats 9 and 5). Depth: 90 fps occurs at 960×600 (formats 9 and 5) **and**
  at 480×300 (formats 24 and 5) — the prior draft's "90 fps at 960×600 on
  both" is true but not the whole depth story.
- **At 1920×1200 in format 9 the ceiling is 50 fps** (both IR and depth).
  Format 5 at 1920×1200 also caps at 50.
- **A 1920×1200@60 profile exists only as format 24, and only on depth**
  (`[0,24,1920,1200,60]`). IR has no 1920×1200@60 at all.
- Depth alone offers 480×300 (format 24 and 5) and a 1600×1000@45 (format 5)
  — entries with no IR counterpart.

## 3. The device-side mode table: flash structure 4035 (`0x0fc3`)

A tenth structure read — not a profile list — carries the device's **scan-mode
table**. It is read with the op-29/30/31 triple
(`INIT/READ/FINISH_READ_STRUCT_DATA_LIST_V1_1`), all three on port 10010
(depth), as `ob_rpc_send_data` frames.

Frame layout (this pass's reading, confirming `protocol-spec.md` §2.1 and
adding the offsets):

```
request  "GM" | u8 class | u16 opcode(BE) | u16 seq(BE) | u8 0 | u32 id(BE)
                                    | u32 offset(BE) | u32 length(BE)   (op 30 only)
response "RB" | u8 type  | u16 opcode(BE) | u16 seq(BE) | u16 status | payload
```

> **Correction (row #144) — the GM/RB header fields are LITTLE-endian, and
> `size` counts the payload's half-words, not the whole frame.** The opcode
> reads `1d 00` = 29 only as LE (`0x1d00` = 7,424 as BE) and the size reads
> `53 00` = 83 only as LE (the payload is 166 bytes = 83 half-words). The
> structure id and the structure payload are little-endian as well. The
> *structure id* field above is also mis-drawn: the id is a plain u32-LE at
> payload offset 0, not a u32-BE. The frame re-read from the raw bytes, and
> the re-derivation of §3's table from them, are in
> `small-segments-row144.md` §2.0 and §2.2.

The 4035 read (session4):

| step | t | op | seq | id | offset | length |
|---|---|---|---|---|---|---|
| INIT | 25.154391 | 29 | 157 | 4035 (`c3 0f 00 00`) | — | — |
| READ | 25.155056 | 30 | 158 | 4035 | 0 | **200** |
| FINISH | 25.155588 | 31 | 159 | 4035 | — | — |

The INIT response carries the total length (200 B); the READ response carries
the bytes at response offset 10, **little-endian u32** (the GM/RB *header*
fields are big-endian; the structure payload is little-endian — easy to get
wrong). 200 B = 50 u32 = **ten 5-tuples**:

| # | tuple | reading |
|---|---|---|
| 1 | `[7,9,1920,1200,50]` | depth, format 9, 1920×1200@50 |
| 2 | `[7,9,1520,1200,60]` | depth, format 9, 1520×1200@60 |
| 3 | `[7,9,1320,1040,60]` | depth, format 9, **1320×1040@60** |
| 4 | `[6,9,1920,1200,50]` | IR, format 9, 1920×1200@50 |
| 5 | `[6,5,1920,1200,50]` | IR, format 5, 1920×1200@50 |
| 6 | `[6,9,1520,1200,60]` | IR, format 9, 1520×1200@60 |
| 7 | `[6,5,1520,1200,60]` | IR, format 5, 1520×1200@60 |
| 8 | `[6,9,1320,1040,60]` | IR, format 9, **1320×1040@60** |
| 9 | `[6,5,1320,1040,60]` | IR, format 5, **1320×1040@60** |
| 10 | `[2,5,1280,800,30]` | first field 2 (not 6/7), format 5, 1280×800@30 |

The tuple shape is the profile-tuple shape `[streamtype, format, w, h, fps]`
(§1) — the same five fields `start_stream` uses, **including** the leading
stream type. The first field is the stream type (6 = IR, 7 = depth, §4); row
10's `2` is a third stream type this capture never streams (RGB is plausible
but unproven — see §8).

So the mode table is: **three (w, h, fps) rows** (1920×1200@50, 1520×1200@60,
1320×1040@60) × the stream types/formats that offer them, **plus one RGB-ish
row**. That is a *mode table*, not a profile list: it names the modes the
device can be driven in. `[inferred]`

**Cross-check worth recording: 1320×1040 appears in the mode table but in
none of the three profile lists** (the lists' resolutions are listed in §1.1 —
no 1320×1040 anywhere). So the flash table is not a subset of the advertised
profiles; the two are independent device-side sets, and the table is the one
that looks like "modes". `[inferred]`

Other structures read the same way in the same capture (ids only — not decoded
here): 4024 (`0x0fb8`), 4026 (`0x0fba`), 4027 (`0x0fbb`), 4034 (`0x0fc2`).
4034 is read at t=25.046, 4035 at t=25.154, 4026/4024 again at t=25.366.

> **Row #144 decoded them.** **4034 (`0x0fc2`) is the device's *named*
> scan-mode table** — seven records of {16-byte GUID, NUL-terminated name,
> u32 code}: "Laser Lines Scan", "Fastor Laser Lines Scan", "Chip Scale
> Speckle Scan", "Sensor Crop Speckle Scan", "Sensor Binned Speckle Scan",
> "Binned Fast Speckle Scan", "Factory Calibration". 4024 is a
> resolution/ROI table (480×300, 1280×800, 960×600, 1920×1200); 4026 is
> 28 bytes of scalars; 4027 is 760 bytes of numeric records. Layouts and
> bytes: `small-segments-row144.md` §2.1–§2.5. §3's mode table above is
> confirmed byte-exactly (§2.2 there).

## 4. The profile actually used: `start_stream` / `stop_stream`

Only **one** profile is ever streamed, on both interfaces:

| call | t | port | params |
|---|---|---|---|
| `ob_rpc_start_stream0-2-12` (IR) | 25.294078 | 10013 | `[6, 9, 1920, 1200, 50]` |
| `ob_rpc_start_stream0-2-10` (depth) | 25.339820 | 10010 | `[7, 9, 1920, 1200, 50]` |
| `ob_rpc_stop_stream0-2-10` (depth) | 111.428486 | 10010 | `[7, 9, 1920, 1200, 50]` |
| `ob_rpc_stop_stream0-2-12` (IR) | 111.447371 | 10013 | `[6, 9, 1920, 1200, 50]` |

The ring tail (`crealityscan-session_00023…pcapng`, frame 27153 @ t=1792378288.25
and 27161 @ .27) carries the same two `stop_stream` calls with the same
params (`95 07 09 cd0780 cd04b032` = `[7,9,1920,1200,50]` on port 10010;
`95 06 09 cd0780 cd04b032` = `[6,9,1920,1200,50]` on port 10013) — the start
calls predate the surviving ring. `[inferred]`

**No other profile was requested anywhere in the capture set.** The five ring
captures (00019–00023) contain no `start_stream` at all and no
`ob_get_stream_profile_list`; session4 contains exactly two of each, above.
The only start/stop params in the whole set are 1920×1200@50 format 9.

So the row's premise — "the advertised blue-laser ceiling is 60 FPS but the
only captured session requested 50 fps" — is explained: **50 fps is what the
vendor client asked for**, even though 60 fps profiles exist on both IR and
depth, and even though the device's own mode table lists 1520×1200@60 and
1320×1040@60. Why the vendor client chose 50 is not on the wire. `[inferred]`

## 5. The realized `get_frame` rate is ~85–86 Hz, not 51.2 Hz

`protocol-spec.md` §7 (and `streaming-replay-payloads.md` §2) state ~51 Hz,
derived from **nine intervals in the first ten IR frames** (0.175681 s → 51.2
Hz). Over the whole capture the realized request rate is far higher:

| interface | requests | first | last | span | rate |
|---|---|---|---|---|---|
| IR (10013) | 7,420 | 25.339738 | 111.423922 | 86.084 s | **86.18 Hz** |
| depth (10010) | 7,260 | 25.358578 | 111.403162 | 86.045 s | **84.36 Hz** |

Per-10 s bucket counts (from the first request): IR 843, 862, 858, 861, 874,
865, 867, 851, 539 (last bucket is truncated — the stream stops at t≈111.4);
depth 833, 855, 850, 861, 863, 810, 837, 811, 540. The rate is **steady at
~85–89 Hz** across every full bucket; the ninth is short, not slow. `[inferred]`

The request cadence is **bimodal** — IR: median inter-request 5.446 ms, p90
23.839 ms, max 136.8 ms (depth: median 5.805 ms, p90 24.007 ms). The client
polls in bursts; a short window (nine intervals) lands in the slow mode and
reads as ~51 Hz, which is where the old figure came from. **The 51.2 Hz
figure is a windowing artifact, not the realized rate.** `[inferred]`

Two consequences worth flagging elsewhere:

- The nominal source rate of the selected profile is 50 fps, but the client
  polls at ~85 Hz. So the daemon is either re-sending the latest frame or
  serving a faster source; `protocol-spec.md` §6 item 3 ("pull semantics")
  stays open, and the 50 fps profile is *not* the reason for the observed
  rate. The rate exceeds the profile's nominal fps, so a frame is not
  guaranteed to be fresh on every poll.
- Anything that assumed "~50 Hz polling" (frame-rate budgets, the row-115
  cross-machine comparison) is working from the wrong number.

## 6. The mode table is device-supplied, not in the SDK binaries

The §3 table lives in the **device's flash** (structure 4035), not in the
host SDK. This pass re-ran the search: 35 vendor dylibs were scanned —
`.recon-cache/liborbbec_scan_arm64.dylib`, `.recon-cache/libOrbbecSDK_arm64.dylib`,
and every `*.dylib` in `CrealityScan.app/Contents/PlugIns` (33, including the
daemon's `libOrbbecSDK.1.7.1.dylib`) — for the **byte patterns** of the
tuples, little- and big-endian:

| pattern | hits |
|---|---|
| `(7,9,1920,1200,50)` LE | none |
| `(9,1920,1200,50)` LE / BE | none |
| `(9,1520,1200,60)` LE / BE | none |
| `(9,1320,1040,60)` LE | none |
| `(1320,1040)` LE / BE | none |
| `(9,960,600,90)` LE, `(960,600,90)` LE | none |
| `(0,0,1280,800,30)` LE (an RGB-list row) | none |
| `(9,1920,1200,30)` LE | none |

The individual constants **do** occur (`1320` appears 468× in
`liborbbec_scan.dylib`, `90` 1213× in `libOrbbecSDK.1.7.1.dylib`, …) — but
never in these combinations. The daemon's SDK copy carries the **Gemini2**
family's code (`libobsensor::Gemini2Device`, 3,615 "Gemini2" hits) and two
unrelated profile tuples (`(9,640,400,30)`, `(9,1280,800,30)`), not ours.
`[inferred]`

*Caveat on the negative result*: absence of a byte pattern is not proof that
no such table exists in another encoding (u16 fields, runtime assembly, or a
compressed/serialized form). The claim is the narrow one: **no static copy of
these triples exists in the host binaries**, so the profile *lists* (§1) and
the mode table (§3) are produced by the device/daemon at runtime and merely
relayed by the SDK — the client asks ("`ob_get_stream_profile_list`"), the
device answers. `[inferred]`

## 7. Corrections this finding makes to other documents

1. `protocol-spec.md` §1.2 — the `ob_get_stream_profile_list` row says
   "array(23) … observed for the RGB interface". Correct as far as it goes,
   but the method is issued on **all three** interfaces (23 / 54 / 89 entries).
2. `protocol-spec.md` §7 — "~51 Hz" → **~85–86 Hz** (§5).
3. `streaming-replay-payloads.md` §2 — the 51.2 Hz cadence line is a
   9-interval window artifact; the realized rate is §5's.
4. `flash-writes-row102.md` §2.2 — `ob_get_stream_profile_list | 1` → **3**
   (one per interface). A correction note is added there.
5. `SPEC-TARGETS.md` (dispatcher's file — **not** edited here) — the 51.2 Hz
   figure there needs the same correction as (2)/(3).

## 8. OPEN QUESTIONS

1. **Which mode name maps to which mode-table row.** 22-line blue / 7-line
   blue / NIR → one of the three (w, h, fps) rows of §3. **Not on the wire
   and not inferable from this capture**: the capture contains one mode only
   (1920×1200@50, format 9), and nothing in the RPC traffic carries the mode
   name. *Needs a new capture*: one CrealityScan session that **switches
   between the three scan modes**, captured on `lo0` with the RPC control
   traffic retained, then look at `ob_rpc_start_stream` on ports 10013 (IR)
   and 10010 (depth) — each mode switch should re-issue `start_stream` with a
   different `[streamtype, format, w, h, fps]`. The device's mode-table rows
   (§3) are the candidate set to match against. Until then this row's
   "which profile does each mode use" stays unanswered.
2. **Why did the vendor client ask for 50 fps** when 60 fps profiles exist
   and the device's own table lists 60 fps modes (§2, §3)? Candidates: a
   host-side frame-rate budget, a scan-quality choice, a cap on the "22-line
   blue" mode. Not on the wire.
3. **What does the mode table's first field `2`** (row 10, `[2,5,1280,800,30]`)
   mean? It is neither IR (6) nor depth (7); RGB is plausible (the RGB list
   has `[0,5,1280,800,30]`, same triple), but the capture never streams it.
4. **What are `format` 0 / 5 / 9 / 24?** The ids are on the wire; their
   names are not.
5. **Pull semantics** (§5): the daemon serves at ~85–86 Hz poll requests for
   a 50 fps profile. Whether each response is a fresh frame or the latest
   re-sent is still open (`protocol-spec.md` §6 item 3).
6. **The other structures** read by the op-29/30/31 triple (4024, 4026,
   4027, 4034) are undecoded; one of them may hold the mode *names*.
   **CLOSED by row #144**: structure **4034 holds the mode names** (seven
   named scan modes — see §3's addendum and `small-segments-row144.md`
   §2.1). What is *not* closed is the **join** from a mode name to a
   profile row: 4035 gives the *selected* mode's ten rows, and nothing in
   this capture links the two, because the session ran one mode. The
   remaining half of OQ1 is the mode-switch capture of row #145 (P6) —
   read **4034 then 4035** at each switch.

## 9. Method / provenance

- Source: `~/DevReposiMac/raptor-protocol-capture/session4/small_payloads.tsv`
  (t, srcport, dstport, hex payload) decoded with the capture's own
  `msgpack_decode.py`; `crealityscan-session_00023…pcapng` via `tshark` for
  the ring tail's `stop_stream` frames.
- Every number above is recomputed from the capture in this pass; the two
  count corrections (§2) and the 51 Hz correction (§5) are the only places
  where this pass departs from the prior session's draft.
- No gated hardware run backs any of this: `[inferred]` throughout.

## 10. Retro pass summary — what is captured and what remains (row #114 retro)

*Added 2026-10-21. This section consolidates the enumeration from the prior
sections and specifies the exact operator capture needed to close the remaining
open questions.*

### 10.1 What is captured

**Per-interface profile lists (complete):**

| Interface | Port | Profiles | Key 60 fps entries |
|-----------|------|----------|-------------------|
| RGB | 10012 | 23 | 1280×800@60 (format 5) — RGB never offers 1920×1200 |
| IR | 10013 | 54 | 960×600@60 (fmt 9), 1280×800@60 (fmt 9,5), 1520×1200@60 (fmt 9,5), 1600×1000@60 (fmt 9) |
| Depth | 10010 | 89 | 960×600@60 (fmt 9,5,24), 1280×800@60 (fmt 9,5), 1520×1200@60 (fmt 9,5,24), 1600×1000@60 (fmt 9), **1920×1200@60 (fmt 24)** |

**60 fps profiles (complete):**

- **IR**: 6 entries — `[0,9,960,600,60]`, `[0,9,1280,800,60]`, `[0,9,1520,1200,60]`, `[0,9,1600,1000,60]`, `[0,5,1280,800,60]`, `[0,5,1520,1200,60]`
- **Depth**: 11 entries — `[0,24,1520,1200,60]`, **`[0,24,1920,1200,60]`**, `[0,9,960,600,60]`, `[0,9,1280,800,60]`, `[0,9,1520,1200,60]`, `[0,9,1600,1000,60]`, `[0,24,960,600,60]`, `[0,5,1280,800,60]`, `[0,5,1520,1200,60]`, `[0,5,1600,1000,60]`, `[0,5,960,600,60]`

**`start_stream` params actually used (complete):**

- IR: `[6, 9, 1920, 1200, 50]` (stream type 6, format 9, 1920×1200@50)
- Depth: `[7, 9, 1920, 1200, 50]` (stream type 7, format 9, 1920×1200@50)
- **60 fps start was never requested** — the vendor client chose 50 fps even though 60 fps profiles exist

**Device-side mode table (structure 4035, complete):**

10 tuples defining three (w, h, fps) modes across stream types/formats:
- 1920×1200@50 (depth fmt 9, IR fmt 9, IR fmt 5)
- 1520×1200@60 (depth fmt 9, IR fmt 9, IR fmt 5)
- 1320×1040@60 (depth fmt 9, IR fmt 9, IR fmt 5)
- RGB-ish row: `[2,5,1280,800,30]`

**Mode names (structure 4034, decoded by row #144):**

Seven named scan modes: "Laser Lines Scan", "Fastor Laser Lines Scan", "Chip Scale Speckle Scan", "Sensor Crop Speckle Scan", "Sensor Binned Speckle Scan", "Binned Fast Speckle Scan", "Factory Calibration"

### 10.2 What is NOT captured

**Mode-name → profile-row join:**

The capture shows **one mode only** (1920×1200@50 format 9 on both IR and depth). Nothing in the RPC traffic maps a *mode name* to a *profile row*. The three scanning modes mentioned in the row's premise (22-line blue / 7-line blue / NIR) are not named on the wire, and the capture contains no mode switches.

**Why this is not inferable from the existing capture:**

- The session ran one mode only (1920×1200@50)
- No mode switches occurred during the session
- The mode names (from structure 4034) are not correlated with `start_stream` calls
- The mode table (structure 4035) lists the *available* modes but not which one is *selected*

### 10.3 Exact operator capture needed

**Objective:** Map mode names to profile rows by capturing a session that switches between scan modes.

**Capture setup:**

1. **Tool:** `raptor_client` with `--capture` and `--dump-replies` flags (same as plan #132)
   - Command: `./build/tools/raptor_client/raptor_client --live 127.0.0.1 <port> <duration> --frames <N> --capture ~/captures/plan114-mode-switch --capture-seconds <duration> --capture-bytes 600000000 --dump-replies`
   - Run on both ports 10013 (IR) and 10010 (depth)

2. **Alternative:** `dumpcap` / `tshark` on `lo0` with BPF filter to capture RPC traffic
   - Filter: `tcp port 10010 or tcp port 10013`
   - Duration: full CrealityScan session with mode switches

3. **Session requirements:**
   - Launch CrealityScan
   - Start in one scan mode (e.g., "Laser Lines Scan")
   - **Switch to a second mode** (e.g., "Fastor Laser Lines Scan")
   - **Switch to a third mode** (e.g., "Chip Scale Speckle Scan")
   - Optionally: switch back to the first mode
   - Stop the session

**What to look for in the capture:**

1. **Structure reads:** `ob_rpc_send_data` with opcodes 29/30/31 (INIT/READ/FINISH_READ_STRUCT_DATA_LIST_V1_1)
   - **Structure 4034** (mode names) — read at session start
   - **Structure 4035** (mode table) — read **after each mode switch**
   - Correlate the structure 4035 reads with the mode switches to determine which profile rows correspond to which mode

2. **Stream starts:** `ob_rpc_start_stream` on ports 10013 (IR) and 10010 (depth)
   - Each mode switch should re-issue `start_stream` with different `[streamtype, format, w, h, fps]` params
   - Match the params against the mode table rows from structure 4035

3. **Expected outcome:**
   - Mode name (from 4034) → mode switch event → structure 4035 read → profile rows → `start_stream` params
   - This gives the join: mode name → profile row

**Analysis:**

- Decode the structures using the same method as `small-segments-row144.md` §2.1–§2.5
- For structure 4035: parse the 200-byte payload as 10 u32-LE tuples
- For structure 4034: parse the GUIDs, names, and codes
- Correlate timestamps: mode switch → structure 4035 read → `start_stream` call

### 10.4 What remains open

**OQ-114-1: Mode-name → profile-row join**

- **Status:** Not captured
- **Why:** The session4 capture ran one mode only; no mode switches occurred
- **Resolution:** Operator capture specified in §10.3 (mode-switch session)
- **Priority:** High — this is the row's headline question

**OQ-114-2: Why did the vendor client ask for 50 fps when 60 fps exists**

- **Status:** Not on the wire
- **Why:** The client's mode-selection logic is host-side, not in the RPC traffic
- **Candidates:** Host-side frame-rate budget, scan-quality choice, cap on "22-line blue" mode
- **Resolution:** Requires host-side code analysis (CrealityScan application) or vendor documentation
- **Priority:** Medium — does not block C2 implementation

**OQ-114-3: What does the mode table's first field `2` mean (row 10)**

- **Status:** Not determined
- **Why:** The capture never streams stream type 2; RGB is plausible but unproven
- **Resolution:** Operator capture that streams RGB (if possible), or daemon code analysis
- **Priority:** Low — RGB is not used in the captured scanning modes

**OQ-114-4: What are format 0 / 5 / 9 / 24**

- **Status:** Not determined
- **Why:** The ids are on the wire but their names are not
- **Resolution:** Daemon code analysis or vendor documentation
- **Priority:** Medium — needed to interpret the profile lists correctly

**OQ-114-5: Pull semantics**

- **Status:** Partially resolved by row #143
- **Why:** The daemon serves at ~85–86 Hz poll requests for a 50 fps profile
- **Resolution:** Row #143 established this is a "latest frame" model, not a queue
- **Priority:** Low — resolved

**OQ-114-6: Other structures (4024, 4026, 4027)**

- **Status:** Undecoded
- **Why:** Not in scope for row #114
- **Resolution:** Decode as needed for other rows
- **Priority:** Low — not blocking

### 10.5 Summary

**Captured (complete):**
- Per-interface profile lists (RGB 23, IR 54, depth 89)
- 60 fps profiles exist on IR (6 entries) and depth (11 entries, including 1920×1200@60 format 24)
- `start_stream` params actually used (1920×1200@50 format 9 on both IR and depth)
- 60 fps start was never requested
- Device-side mode table (structure 4035, 10 tuples)
- Mode names (structure 4034, 7 named modes)

**Not captured (requires operator capture):**
- Mode-name → profile-row join (which scanning mode selects which profile)

**Operator capture needed:**
- Mode-switch session (specified in §10.3)
- Read structures 4034 and 4035 at each mode switch
- Correlate with `start_stream` calls to determine the join

**Row #114 verdict:**
- **60 fps profiles exist:** Yes (on IR and depth)
- **Which profile each mode uses:** Not captured — requires mode-switch session
- **Row status:** SPEC-READY for the enumeration; the mode-profile join is an operator capture task (row #145)
