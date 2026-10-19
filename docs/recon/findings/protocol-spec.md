# The Raptor host protocol — Phase A spec (row #96 / G-A)

*Observation-derived from the 2026-10-18 capture set (five rotated pcapng on
`lo0`, the `session2/`–`session4/` extras, and the 354,430-line
`small_payloads.tsv` digest) and the public OrbbecSDK v2.10.6 source (the checkout's `src-read/LICENSE.txt` claims MIT; the upstream license is not confirmed — see the erratum in `FULL-INDEPENDENCE.md`)
(`src-read/src_device_protocol_Protocol.hpp`). **Wire observation only — no
vendor binary was read.** Every substantive claim is `[inferred]`
(capture-derived); nothing here is a gated run. This is what the builder
implements from; the captures themselves stay outside the repo.*

## 0. Provenance and scope

- The capture set: `crealityscan-session_00019..00023` (the ring's surviving
  tail of a full CrealityScan session, 2026-10-18), `session2/session2.pcapng`
  (268 KB), `session3/session3.pcapng` (4.9 KB), `session4/session4.pcapng`
  (217 MB) plus its `small_payloads.tsv` (t, srcport, dstport, hex payload —
  payloads up to ~1 KB; the big frame segments are only in the pcap).
- The public header cross-reference: the framing, opcode enum and request /
  response structs in `Protocol.hpp` match the wire exactly where cited
  below; the one opcode used for calibration reads is **not** in that header
  (§2).
- Clean-room posture: facts are the wire bytes and the public header; no vendor
  binary, no decompiled output. All `[inferred]` until a gated run re-confirms.

## 1. The RPC method set

### 1.1 Framing and transport

- msgpack-RPC (rpclib-style) over **TCP**, one connection per interface:
  requests `[0, msgid, method, params]`, responses `[1, msgid, nil, result]`.
  Observed directly in the TSV (e.g. `940001be` + `"ob_get_device_enumeration_info"`
  + `90`; response `940101c0` + the result). `[inferred]`
- One method name carries a **suffix** identifying the device and interface:
  `<device-id><interface-index>` with device id `0-2-1` and interface index
  **0 = depth, 2 = IR, 4 = RGB, 6 = IMU** — the same indices the enumeration
  result carries. Observed suffixes: `0-2-10`, `0-2-12`, `0-2-14`, `0-2-16`.
  `[inferred]`
- Interface → TCP port (from the enumeration result and the port↔method
  correlation): depth `10010`, IMU `10011`, RGB `10012`, IR `10013`. The
  suffix's last digit and the port number are different numbering schemes
  (e.g. IR = suffix `…12`, port `10013`). `[inferred]`
- Call pattern: **pull-based, one call per frame/sample.** Observed counts
  (session4, an 86 s pull window): `get_frame` 7,260 (depth) / 7,420 (IR);
  `get_imu_frame` 15,892; `get_device_report_infomation` 671; `send_data`
  3,336. `[inferred]`

### 1.2 The methods observed (21), with params and results

| method | params (msgpack) | result |
| --- | --- | --- |
| `ob_get_device_enumeration_info` | `90` (empty) | array of 4 entries: {device id `"0-2-1"`, u16 `0x2bc5`, u16 PID `0x0c03`, serial `"CR0886100SN"`, name `"Creality Raptor Pro …"`, interface index 0/6/4/2, ip `"127.0.0.1"`, port} — the port map (10010/10011/10012/10013) |
| `ob_get_stream_profile_list` | `90` | array(23) of `[a, b, w, h, fps]` (e.g. `[0,0,1280,800,30]`, `[0,0,1280,800,20]`, … `[0,0,640,400,10]`) — observed for the RGB interface |
| `ob_rpc_get_frame` | `90` | a msgpack **bin** — the frame (see §1.3) |
| `ob_rpc_get_imu_frame` | `90` | a 504-byte sample string, **or empty** (`a0`) when no new sample — the poll-returns-nothing pattern |
| `ob_rpc_get_device_report_infomation` | `90` | a 248-byte sample, or empty (584 of 671 empty) |
| `ob_rpc_start_stream` | `95 06 09 cd0780 cd04b032` = `[streamtype, 9, 1920, 1200, 50]` where **streamtype = 6 on IR (port 10013), 7 on depth (port 10010)** | `true` (msgpack bool) |
| `ob_rpc_stop_stream`  | `95 06 09 cd0780 cd04b032` (IR) / `95 07 09 cd0780 cd04b032` (depth) — **same** streamtype as start | (large / not captured small) |
| `ob_rpc_start_imu_stream` / `ob_rpc_stop_imu_stream` | `90` | `true` |
| `ob_rpc_get_pu` | `[2002]` | uint16 `6000` |
| `ob_rpc_get_pu_range` | `[2002]` | array of four `bin8(4)` values: `[1000, 15000, 100, 1000]` |
| `ob_rpc_set_pu` | `[2002, <value>]` where value varies (observed 1000–6000) | (ack) |
| `ob_set_ir_frame_light_type` | `[streamtype, 65535]` (streamtype 6 on IR, 7 on depth — same as `start_stream`'s first param) | (ack) |
| `ob_rpc_send_data` | `[<vendor frame bytes>, <expected response size>]` | the vendor response (§2) |

`[inferred]` — the start/stop parameter array reads as `[streamtype, format,
width, height, fps]` (streamtype = 6 on the IR port, 7 on the depth port — the
method name carries start/stop; the array does not); the format field (9) and
the exact field names are not on the wire. `ob_set_ir_frame_light_type`'s first
argument is the same streamtype (6/7), not a light type (§7).

**Extension (row #114)**: `ob_get_stream_profile_list` is issued on **all
three** interfaces — 23 entries (RGB, port 10012), 54 (IR, 10013), 89 (depth,
10010) — not only the RGB one; the entries are
`[0, format, width, height, fps]`, i.e. `start_stream`'s params 2–5. See
`stream-profiles-row114.md` §1–§2.

### 1.3 The frame response (what Phase B must decode)

- The response is `[1, msgid, nil, bin(N)]` — a msgpack **bin**, not a str:
  observed heads `9b 00 cf 00` = `bin8(52,992)` and many smaller bins; the
  capture's typical frame response on 10010→63385 is ~10 KB (a dark session).
- The payload is **not raw pixels**: a 1920×1200 8-bit frame would be 2.3 MB;
  the observed bins are ~10–53 KB → the frame stream is encoded/compressed.
  The codec is **not settled by these captures** (open, §6).
- Port volumes over the 86 s window: 10010→63385 = 167,749 segments /
  72.7 MB; 10013→63400 = 162,127 / 71.4 MB. Segment sizes are mostly ~10.5 KB
  plus many small (208 B) segments (~146k/147k each — role unsettled, §6).
- Whether each response is a *new* frame or the latest frame re-sent on a
  fast poll is not settled (needs payload comparison). `[inferred]`

## 2. The vendor-command framing inside `ob_rpc_send_data`

### 2.1 The framing (public `Protocol.hpp` — matches the wire)

```c
#define HP_REQUEST_MAGIC  0x4d47   // "MG" (wire bytes 47 4d)
#define HP_RESPONSE_MAGIC 0x4252   // "BR" (wire bytes 52 42)
struct ReqHeader  { u16 magic; u16 sizeInHalfWords; u16 opcode; u16 requestId; };            // 8 B
struct RespHeader { u16 magic; u16 sizeInHalfWords; u16 opcode; u16 requestId; u16 errorCode; }; // 10 B
```

`[inferred]` — observed exactly: request `474d 0200 0300 0000` + payload;
response `5242 5300 0300 0000 0000` + payload (the size field counts
half-words including the header; the response `requestId` matches the
request's).

The second msgpack parameter of `send_data` is the **expected response buffer
size** — observed values 64 / 512 / 1024 and they match the response string
sizes exactly (64 = ack-sized responses for ops 1/2/26/28/29/31/17/19; 512 =
structure data for ops 3/27; 1024 = bulk for ops 18/25/30). `[inferred]`

### 2.2 The opcodes observed (session4 counts)

| opcode | public name (Protocol.hpp) | n | notes |
| --- | --- | --- | --- |
| 1 | `GET_PROPERTY` | 267 | payload `{u32 propertyId}`; response `PropertyData {i32 cur,max,min,def,step}` |
| 2 | `SET_PROPERTY` | 2,869 | payload `{u32 propertyId, i32 value}` — the laser/exposure loops (§3) |
| 3 | `GET_STRUCTURE_DATA` | 8 | payload `{u32 structureId}` (id 1000 = device info: version strings "1.5.7", "V1.0", "1.0.2", "MX6600", "Null", serial, model) |
| 17/18/19 | `INIT/READ/FINISH_READ_RAW_DATA` | 2/4/2 | structure 0x0fc4 |
| **25** | — **not in the public enum** | **120** | the flash memory read (§4) |
| 26 | `GET_COMMAND_VERSION_V1_1` | 16 | |
| 27 | `GET_STRUCTURE_DATA_V1_1` | 17 | e.g. id 1043 |
| 28 | `SET_STRUCTURE_DATA_V1_1` | 1 | id 1043 |
| 29/30/31 | `INIT/READ/FINISH_READ_STRUCT_DATA_LIST_V1_1` | 10/10/10 | e.g. id 4035 read in 200-byte steps; the data comes back as u32 tuples (the stream profiles `{7,9,1920,1200,50}`) |

`[inferred]` — opcode 25 sits in the gap the public header leaves (19→26): it
is a daemon/firmware extension whose shape is only on the wire (§4).

## 3. The property map

The ids are what the wire carries; the semantic names are the plan's labels
(§2 of `FULL-INDEPENDENCE.md`) — binding each id to a UI action needs
single-action captures (the plan's OQ-F2). `[inferred]`

### 3.1 Host-driven loops (the high-count setters)

- **props 3, 152, 153** — SET ~890× each over 86 s (889× value 0, 3–4× value
  1). The laser on/off + laser-group trio; they stop during pauses (plan §2).
- **props 154, 155** — float-valued (values are float bit patterns: 500000,
  1.4e6, 800000, 866000…), SET 27× each; GET ranges: cur 1.4e6, max 1.5e6,
  min ~1.0e5, def ~8.1e5, step ~5.9e3 — exposure/current-like.
- **prop 5** — float-valued: GET cur 110000, same range shape; SET 6×.
- **prop 240** — float-valued: GET cur 800000, max 1.5e6, min 500000.

### 3.2 Flags, enums, sliders (observed ranges)

- 151 (0/1), 157 (0), 158 (0/1), 183 (0/1), 192 (0..8), 186 (0..1), 187
  (0..2), 164 (0/2/3/5 — an enum), 160 (0..100, cur 100), 161 (0..100, cur
  50), 2022 (1..15, cur 8, def 5), 2024 (1..4, cur 2), 2026 (100..3000, cur
  2403, def 3000 — SET 19× with values 3000/2990/2620/2403…), 181 (cur
  1792014971, max −1 — timestamp/version-like), 85/89 (GET only; 89 SET 0×2).
- GET counts: 154(47), 155(46), 5(43), 160(40), 2026(29), 240(20), 161(20),
  158(6), 192(6), 85/89/181(2), 186/2022/2024/187(1).

### 3.3 The PU channel (a parallel id space)

`get_pu` / `get_pu_range` / `set_pu` use their own ids — observed **2002**
(read: 6000; range `[1000, 15000, 100, 1000]`) for both read and set operations.
`set_pu` values vary (observed 1000–6000 across 45 calls). 69 get_pu / 9 get_pu_range / 45 set_pu calls, on the IR interface. `[inferred]`

## 4. The flash-read transport (op 25) — cross-ref, not duplicated

- Request payload `{u32 address, u16 sizeInHalfWords}`; observed **120 reads ×
  384 hw (768 B)**, the last 256 hw. The response is the raw window inside a
  fixed 1024-byte msgpack string. `[inferred]`
- The first response decoded: the record header at 0x200000 = `{u32 magic
  0x46447311, u32 CRC-32, u32 length 0x1a7c, u32 address 0x200000}` — exactly
  the hand-back's corrected record header (magic `0x464473<kind>`, the fourth
  word = the record's own address). `[inferred]`
- Read **runs** (session4 timestamps): startup t≈2.88–2.95 s —
  `0x200000..0x204000` and `0x20a000..0x218000`; project-open t≈25.37–25.38 s
  — `0x0b0000..0x0b2000` and `0x204000..0x206000`.
- Record-level map, the plane/backup copies, the checksum and the consumers:
  see `calibration-store.md` §1 (esp. §1.4's map) and the preserved hand-back
  `calibration-wire-handback-2026-10-18.md` — not repeated here.

## 5. The discovery / transport layer

- **GVCP on UDP 3956.** The only GVCP packets in the surviving windows are
  **DISCOVERY_CMD** — 8 bytes `42 11 00 02 00 00 00 01` (key `0x42`, command
  2, length 0, request id 1) — sent periodically (~every 2 s) from three local
  addresses (`127.0.0.1:…`, `172.22.20.183:…`, `172.22.20.198:…`) to UDP 3956
  (localhost and broadcast), seen on the `lo0` capture. `[inferred]`
- **The daemon's DISCOVERY_ACK is not in these windows.** The plan's "the
  daemon answers with model/serial/RPC-port" is not re-verified here — the
  ring rotated out the launch window (22:48:21), and the app already knows
  port 10010 before its first enumeration call. The bootstrap bytes need a
  startup-window capture (open, §6). `[inferred]`
- **TCP:** the RPC connections (ephemeral client ports ↔ 10010/10011/10012/
  10013) carry both the RPC traffic and the frame streams; the IMU stream on
  10011; RGB (10012) was configured (profile list, PU) but not streamed in
  this session.

## 6. What the captures cannot settle (open list)

1. **The frame payload encoding** — the bins are ~10–53 KB for a 1920×1200
   8-bit stream, so they are encoded; the codec/container is not derivable
   from these captures alone. Phase B/C need this (a targeted capture with
   known content, plus the vendor's own output as the A/B reference).
2. **The small (208-B) segments** on the frame ports (146k on 10010, 147k on
   10013, ~30 MB each) — role unsettled (a second sub-stream? a preview? a
   segmentation artifact?).
3. **Pull semantics** — whether each `get_frame` response is a fresh frame or
   the latest re-sent (the ~85/s call rate vs the stated 50 fps source).
4. **The DISCOVERY_ACK** (the bootstrap carrying the RPC port) — needs the
   launch window.
5. **The property-id → semantic binding** (plan OQ-F2) — needs short
   single-action captures (one pause/resume, one mode switch, one slider).
6. **The IMU sample's layout** — a 504-byte string with float64-looking
   timestamps (~1.87e9) and float samples, zero-padded; not decoded.
7. **The PU id space** (2002/2003 seen) and its full method set.
8. **The exact op-25 returned length** vs the requested 768 B (the RespHeader
   size field vs the msgpack string's fixed 1024 B) — one targeted read with
   a decoded length would settle it.
9. **What needs a decision, not a capture:** none for this spec; the plan's
   G-B (macOS privileged access shape) is Phase D's.

## 7. The streaming sequence (replay payloads, retired OQs)

**Retired OQs**: §6 item 3 (pull semantics / cadence) is now settled from the
capture: the vendor client calls `get_frame` immediately after `start_stream`
(first response at 45.76 ms), with params `90` (empty array) — matches our
client. The daemon responds with a non-empty bin immediately (no empty polls),
so the daemon buffers frames server-side.

**Correction (row #114)**: the call rate is **~85–86 Hz**, not the ~51 Hz
stated here before — IR 7,420 calls / 86.08 s = 86.2 Hz, depth 7,260 /
86.05 s = 84.4 Hz, steady across every full 10 s bucket. The old 51.2 Hz came
from a nine-interval window of the first ten frames (the cadence is bimodal:
median 5.45 ms, p90 23.8 ms). See `stream-profiles-row114.md` §5.

**The streaming sequence, from the vendor's client** (session4, t=25.26–25.36):

1. **Pre-stream setup** (t=25.259–25.261): `ob_set_ir_frame_light_type` on both
   IR (port 10013) and depth (port 10010), params `[streamtype, 65535]` where
   streamtype = 6 (IR) or 7 (depth). The first parameter is **not** a "light
   type" — it's the stream/interface type, matching the profile tuple
   `{7,9,1920,1200,50}` read from flash (op-31). Called twice per interface
   (before and after start_stream).

2. **Property configuration** (t=25.281–25.283): three `SetProperty` calls on
   depth (port 10010): prop 157=0, prop 158=1, prop 2026=2403. These are
   pre-stream configuration (not the laser loop, which starts after frames
   flow).

3. **Start stream** (t=25.294–25.340): sequential, IR first, depth second.
   - IR (port 10013) at t=25.294: `[6, 9, 1920, 1200, 50]` → response `true`
   - Depth (port 10010) at t=25.340: `[7, 9, 1920, 1200, 50]` → response `true`

   The first parameter is the **stream type** (6=IR, 7=depth), **not** a
   start/stop command (spec §1.2 had this reversed). The method name carries
   start/stop; the parameter array is `[streamtype, format, width, height, fps]`.

4. **First frame response** (t=25.340): IR port 10013 returns a bin at
   25.339836, 45.76 ms after start_stream request. The client immediately
   begins `get_frame`, at ~85–86 Hz over the whole capture (the 51.2 Hz in
   `streaming-replay-payloads.md` §2 is a nine-interval window artifact —
   `stream-profiles-row114.md` §5).

5. **Post-stream property sets** (t=25.358–25.364): props 2022=8, 2024=2,
   2020=1, 3=0, 152=0, 153=0, 151=1. Props 3/152/153 are the laser loop
   (§3.1), set to 0 here (laser off during preview?); the loop toggles
   during active scanning.

**Critical diff from our client**: we send `[6, 9, 1920, 1200, 50]` to **both**
interfaces (the spec's §1.2 misread the first param as a command). The vendor
sends `[6, ...]` to IR and `[7, ...]` to depth. This is the likely cause of the
zero-frames gap: our client sends the wrong stream type to the depth interface.

**Replay payloads**: see `streaming-replay-payloads.md` for byte-exact hex.

*All of the above is `[inferred]` from the 2026-10-18 captures; none of it is
a gated run record. The captures and the decode material stay outside the
repo (`~/DevReposiMac/raptor-protocol-capture/`).*
