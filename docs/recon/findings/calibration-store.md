# The calibration store — intrinsics, laser planes, and the thermal model (row #94 / OQ-S15)

*Static half. Addresses and shapes, no decompiled output. §17.4's intrinsics
path (`scan-live-data.md`) is taken as given; this note completes its 41-field
map and adds the laser-plane and thermal items.*

*Follow-up (row #94 corrections): the out-of-band wire hand-back of 2026-10-18
is preserved at `calibration-wire-handback-2026-10-18.md` and folded in below —
wire facts are tagged `[inferred, wire-observed 2026-10-18]` and are not a
gated run record.*

## 0. The three stores at a glance

| item | authoritative store | live copy | C-ABI reachability |
| --- | --- | --- | --- |
| intrinsics | session cache `<output_dir>/resources.obscan` (SQLite; record `camera_intrin`, row `files.name='camera_intrin.bat'`, 210 B) — written from the device's `DeviceParameters` at preview/new-scan start | `device::DeviceParameters` (device layer) | read with any SQLite client + §3's map; no getter |
| laser planes | device **flash records** (16-byte header + payload; magic `0x464473<kind>`, kind 0x14 = planes; payload 5180 B `[wire-observed]`) | 5120-B struct in the device-param cache (`ScanParamProcessor::getDeviceCacheParam()` + 0x10) | none (internal device layer) |
| thermal | SDK-hardcoded coefficients (Raptor) + flash calibration entries + the device temperature | the device-layer conversion result | only a toggle: `obscan_device_management_open_temperature_compensation(bool)` @`0x14728` |

## 1. Laser planes

### 1.1 Where they live — three representations

- **Device flash (authoritative).**
  `DeviceParamAccessor::updateLineLaserLightPlaneParam(const char*, unsigned)`
  @`0x23f324` validates a blob — {u32 magic `0x464473<kind>` @+0 (the low byte
  is a per-kind tag; 0x14 = planes) `[inferred, wire-observed 2026-10-18]`,
  u32 **CRC-32** @+4, u32 length @+8, u32 **address** @+0xc} — then writes it
  with `AbstractDeviceHandle::updateFlashData(0x46447314, 0x211000, data, len,
  1)` @`0x315638`. The second key is the record's own flash address: the
  write-side disassembly reads `mov w2,#0x1000; movk w2,#0x21,lsl#16` =
  `0x211000` (the earlier revision's `0x212000` was a transcription error), and
  the wire sees the record land at `0x211000` `[inferred, wire-observed
  2026-10-18]`. The header's type field equals the record's own flash address
  generally, not a per-record-type constant `[inferred, wire-observed
  2026-10-18]`. Reached from
  `OrbbecInternalDevice::update_line_laser_light_plane_param` @`0x1eb27c` ←
  `device::OrbbecDevice::update_line_laser_light_plane_param` @`0x1b598c`.
- **The checksum is a standard reflected CRC-32** [inferred — static]: the
  validator's table @`0x2ceb980` is byte-for-byte the poly-`0xEDB88320` table
  (table[1] = `0x77073096`), the accumulator starts at `0xFFFFFFFF`
  (`0x23f39c`), the update is the byte-wise `crc = table[(crc^b)&0xff] ^
  (crc>>8)` loop (`0x23f3ac`–`0x23f3c4`), and the result is complemented
  (`mvn` @`0x23f3c8`) before comparison with the stored word. Coverage: the
  whole record from its start for `length` bytes, with the checksum word
  (bytes 4..7) zeroed (`str wzr,[x19,#0x4]` on the copy @`0x23f394`). The
  wire's brute-force did not reproduce it because the coverage/zeroing was
  guessed; the write-side validator above is the definition. Validation against
  the nine captured records is pending their bytes (outside the repo).
- **The backup record.** `DeviceParamAccessor::getBackupLineLaserLightPlaneParams()`
  @`0x24211c` reads 8192 B (`readFlashData(key, 0x2000, vector)` @`0x315a8c`)
  with the flash key from a **per-device-type table** @`0x2cebeb0` (types
  0xc05..0xc10; entries alternate `0xa400` / `0x213000`, out-of-range →
  `0x213000`), then magic-checks the buffer. Caller:
  `LineLaserAbstractDeviceHandle::checkDeviceNecessaryParamStatus`
  @`0x360480`/`0x3604f0`/`0x360558`. The captured device resolves to a
  `0x213000` entry, and the wire sees the backup copy at `0x213000` with a
  checksum differing from the `0x211000` primary — the backup-vs-user split
  `[inferred, wire-observed 2026-10-18]`.
- **The read transport is observable without libOrbbecSDK** [inferred,
  wire-observed 2026-10-18]: the host reads flash with vendor opcode 25
  (memory read) tunnelled through the RPC method `ob_rpc_send_data`, framed
  with the vendor header (magic `0x47` 'MG', size/opcode/requestId); responses
  use the `0x4252` 'BR' framing, 768 B per read (120 reads in the captured
  session). This settles OQ-S15d's transport half.
- **The calibration temp info** (inside the line-laser record family):
  `getCalibrationTempInfoFromLineLaserParam()` @`0x23faac` reads flash key
  `0x14000`, 8192 B, and parses a length+checksum section. Caller:
  `SermoonSerialAbstractDeviceHandle::configDeviceParam` @`0x4483d4`.
- **The live user copy.** `OrbbecInternalDevice::get_line_laser_light_plane_param()`
  @`0x1eb208` copies **5120 B (0x1400)** out of
  `device::ScanParamProcessor::getDeviceCacheParam()` (object +0x10) — the
  plane struct as the pipeline sees it.

### 1.2 The write chain (calibration → flash)

`OrbbecCalib::DownloadCalibParamsIntoScanner` @`0xbdea6c` →
`TransLaserParams2SOC(vector<__ScannerCalibParams__>&, u8*, …)` @`0xd123dc`
(also called from `CalibRaptorMiniFunc` @`0xbe3700`,
`calibration_raptor_pro_func` @`0xbe5110`, `calibration_sermoon_func`
@`0xbe7cb0`) → `ConvertScannerCalibParams2LineLaserParams` @`0xd11b74` → a SoC
image made of **named 4096-B files** uploaded per entry via
`UploadFileContent2Buffer` @`0xcd4e6c` — one of them `/line_laser_flash.bin`
(string @`0x431c223`, referenced at `0xd13268`). Companions:
`SaveCalibrationParams(std::string, __ScannerCalibParams__&)` @`0xbfbf00`
(writes a **text** file — the `camera_intrin.txt` family),
`OriginCalibParams2LineLaserCalibParams` @`0xbfe074`,
`DeviceParameters2LineLaserCalibParams` @`0xc007ec`, `checkUsedParamsValid`
@`0xbfce38`.

### 1.3 Format and units

- The **flash record format is settled**: 16-byte header {magic
  `0x464473<kind>`, CRC-32, length, address} + payload; length and address
  validated on write (the address doubles as the header's type field); CRC-32
  table at `0x2ceb980`.
- The **payload shape** is per-line coefficient rows:
  `ConvertScannerCalibParams2LineLaserParams` @`0xd11b74` zero-fills
  `LineLaserParams` (5488 B = 0x1570) and fills a header (+0x0..+0x14: four
  floats + two int→float counts), then **repeated 8-float groups read with a
  row stride from the calib struct's per-line tables** (+0x18..+0x34,
  +0x50..+0x6c, +0x88..+0xa4 — column walks `[base + n·k]`, k∈{1,2,3,4,5,6,8}),
  then sparse groups at +0xa8..+0xe0 and +0x1508..+0x1530. So the struct is
  **N laser lines × 8 coefficients** (three such tables) plus scalars. The
  per-coefficient **units are not determinable from the conversion** [open].
- Sizes to keep straight: flash read window 8192 B; flash record payload
  **5180 B (0x143c)** `[inferred, wire-observed 2026-10-18]`; `LineLaserParams`
  5488 B; the live user copy 5120 B; the SoC files 4096 B. The 60-byte
  flash-vs-live difference (5180 vs 5120) is unaccounted for — header/trailer
  vs struct body, or flash-only fields (OQ-S15a).

### 1.4 The flash map as read at startup / project-open

Wire-observed `[inferred, wire-observed 2026-10-18]` — nine records, each with
the 16-byte header (the header's type field = the record's own address):

| flash address | magic kind | length | when |
| --- | --- | --- | --- |
| 0x200000 | 0x11 | 6780 (0x1a7c) | startup — primary |
| 0x202000 | 0x11 | 6780 | startup — copy of 0x200000 |
| 0x204000 | 0x08 | 2952 (0xb88) | startup |
| 0x20a000 | 0x12 | 6646 (0x19f6) | startup |
| 0x20e000 | 0x13 | 12066 (0x2f22) | startup |
| 0x211000 | 0x14 | 5180 (0x143c) | startup — **laser planes** |
| 0x213000 | 0x14 | 5180 | startup — plane backup copy |
| 0x215000 | 0x15 | 12066 (0x2f22) | startup — same size as 0x20e000 |
| 0x0b0000 | 0x08 | 3916 (0xf4c) | project-open (t≈25 s) |

Reads are contiguous 768-B windows over 0x200000..0x206000 and
0x20a000..0x218000 at startup, plus 0x0b0000..0x0b2000 at project-open.
0x20e000 (kind 0x13) and 0x215000 (kind 0x15) — same length, adjacent — look
like another primary/backup pair (OQ-S15f).

### 1.5 What is not settled

- The 5120-B user struct's internal layout (a wholesale cache copy; its
  producer was not traced).
- Which of the three 8-float tables is "the plane" (vs distortion/exposure
  rows) and the per-coefficient units.
- Whether any of this reaches the session cache (the record-name inventory
  shows `camera_intrin.bat` only; a plane record, if any, needs a session-dir
  listing).
- The identity of the other flash records (kinds 0x08/0x11/0x12/0x13/0x15) and
  the 0x20e000/0x215000 pairing (OQ-S15f).

## 2. The thermal model

### 2.1 What exists

- **A line-laser temperature compensator** (device layer):
  `Raptor::LineLaserTempCompensator::set_temp_comp_coeff(temp_compensate_coeff)`
  @`0x289320` copies **11 floats** (coeff struct +0x8..+0x30 → this+0xb4..+0xdc);
  `temp_comp_control(temperParams)` @`0x289844` emits 11 floats — the stored
  default block (+0x10..+0x38) or the computed block (+0xec..+0x114) — and
  **recomputes only when |ΔT| > 0.4** (last temps at +0xa0/+0xa4/+0xa8; base
  temps set via the +0x94/+0x9c setter @`0x289344`).
   *[ERRATUM 2026-10-19: superseded — threshold is 0.2°C; settled in `calibration-store-row95.md`]*
- **The coefficient source.** `device::RaptorSerialDeviceHandle::getTemperatureCompensationCoeff()`
  @`0x3d61f0` is a **constant stub** — a 16-float struct zeroed, with `-0.03`
  at indices 2,3 and 6,7 and `0.002` at index 10. On this device the
  coefficients are **hardcoded in the SDK**, not calibrated per unit
  [inferred]. (The Sermoon family delegates to
  `SermoonSerialAbstractDeviceHandle::getTemperatureCompensationCoeff`
  @`0x449db8` — a device-reading variant.)
- **Where it is applied.** Inside **`getDeviceTemperature()`** on every
  Raptor-family handle — Pika @`0x3acc44`/`0x3acd58`, RaptorSerial @`0x3d1d1c`,
  SermoonP1 @`0x3faf34`, SermoonS1 @`0x41fc38`, SermoonX1 @`0x46dea4` — i.e.,
  the compensation runs as part of the temperature query.
- **A temperature-parameterized calibration conversion.**
  `device::AlgorithmProcessor::convertBackupParamToUserParam(int, DeviceTemperature)`
  @`0x216758` (+ `…ForSoc` @`0x2180c8`) reads the **origin calibration params**
  (`DeviceParamAccessor::getOriginCalibrationParams(u32*)` @`0x23fd70`, called
  ×3 at `0x2167d8`/`0x216828`/`0x21687c`) and converts backup→user params
  **per temperature**. That is the calibration-side thermal model.
- **The scan side only polls.** `OrbbecScanContext::HardwareDetectWorker::update_temperature_if_needed(…)`
  @`0x8fcc`: a 10 s interval (0x2540BE400 ns), `device::OrbbecDevice::get_device_temperature()`
  @`0x1b5840`, a device-type-dependent pick (types 0xc10/0xc0b/0xc05 vs an
  average of two), stored as a float at `[worker+0x88]+0xf8`.
- **The only C-ABI surface** is the toggle:
  `obscan_device_management_open_temperature_compensation(bool)` @`0x14728` →
  `OrbbecDevice::open_temp_compensation(bool)` @`0x1b5944` →
  `OrbbecInternalDevice::open_temp_compensation(bool)` @`0x1eacb4`.

### 2.2 What is not settled

- The numeric behaviour: which params the 11 floats correct and their units
  (a runtime sample — the compensator's inputs/outputs, or a diff of the user
  params across a temperature step).
- Whether the 0.4 threshold is °C (assumed; the values come from the device's
  temperature struct) [inferred].
  *[ERRATUM 2026-10-19: superseded — threshold is 0.2°C; settled in `calibration-store-row95.md`]*
- Whether the flash "calibration temp info" (key `0x14000`) holds
  per-temperature *entries* or coefficients (the parser's fields beyond the
  checksum were not read).

## 3. The intrinsics' 41-field map (completing scan-live-data.md §17.4)

`SerializableDeviceParams::to_buffer` @`0x6e3874` (0x554 B) packs 41 fields in
this order (position → type → source offset in `device::DeviceParameters`);
anchored semantics in bold:

| # | type | offset | semantic |
| --- | --- | --- | --- |
| 1 | i32 | (stack local) | computed at pack time — not a struct field [open] |
| 2 | i32 | +0x21a68 | [open] |
| 3 | i32 | +0x21a6c | [open] |
| 4 | f32 | +0x0 | **depth fx** |
| 5 | f32 | +0x4 | **depth fy** |
| 6 | f32 | +0x8 | **depth cx** |
| 7 | f32 | +0xc | **depth cy** |
| 8 | f32 | +0x18 | **depth scale** (= DeviceParameters+0x21a30 × 1e-4) |
| 9 | i32 | +0x10 | **depth width** |
| 10 | i32 | +0x14 | **depth height** |
| 11 | f32 | +0x1c | **colour fx** |
| 12 | f32 | +0x20 | **colour fy** |
| 13 | f32 | +0x24 | **colour cx** |
| 14 | f32 | +0x28 | **colour cy** |
| 15 | i32 | +0x2c | **colour width** |
| 16 | i32 | +0x30 | **colour height** |
| 17–28 | f32 ×12 | +0x21a38..+0x21a64 | **the D2C/extrinsic 12-float block** (helpers @`0x317710`/`0x317784` write it; the last three scaled) |
| 29 | f32 | +0x21a28 | [open — the neighbouring 4-float group] |
| 30 | f32 | +0x21a34 | [open] |
| 31 | f32 | +0x21a2c | [open] |
| 32 | f32 | +0x21a30 | [open — the depth-scale *source* (×1e-4) read by copyDepthParams] |
| 33 | i32 | +0x21ae8 | [open] |
| 34 | i32 | +0x21aec | [open] |
| 35 | u32 | +0x21b08 | [open] |
| 36 | str | +0x21ab8 | version string [which one: open] |
| 37 | str | +0x21a70 | version string [open] |
| 38 | str | +0x21a88 | version string [open] |
| 39 | str | +0x20000+x21 | version string, big-offset block [open] |
| 40 | str | +0x20000+x21′ | version string, big-offset block [open] |
| 41 | str | +0x21af0 | version string [open] |

Anchors: `AbstractDeviceHandle::copyDepthParams` @`0x3176d8` (4 floats→+0x0,
2×int16→+0x10/+0x14, scale at +0x18 from +0x21a30×1e-4),
`copyColorParams` @`0x3176b0` (4 floats→+0x1c, 2×int16→+0x2c/+0x30, 8
floats→+0x34/+0x44), and the D2C writers @`0x317710`/`0x317784`. The colour
block agrees with §17.4's anchor (packed 11–16 = +0x1c..+0x30) — an
independent cross-check.

**The consumer's minimum**: positions 4–16 give both camera models
(fx,fy,cx,cy,w,h) plus the depth scale; 17–28 the extrinsic. The version/flag
fields are not needed to use the intrinsics.

## 4. What a consumer needs to reproduce each SDK-free

- **Intrinsics — settled.** Read `<session>/resources.obscan` (SQLite), row
  `files.name='camera_intrin.bat'` (210 B), parse the 41-field big-endian
  record with §3's map. No device, no SDK.
- **Laser planes — gap.** No C getter; device-flash-authoritative. To
  reproduce SDK-free: (a) a **runtime capture** — dump the session dir for a
  plane record (record-name inventory not established) and/or the device-cache
  struct (5120 B) at scan time; (b) a **device-protocol flash read** (the
  `readFlashData` transport lives behind `AbstractDeviceHandle` /
  libOrbbecSDK); (c) the **calibration-tool path** — `SaveCalibrationParams`
  @`0xbfbf00` writes the text form during the calibration flow.
- **Thermal — partial.** The coefficients are constants in this binary
  (`getTemperatureCompensationCoeff` @`0x3d61f0`) and the compensator's
  algorithm is statically readable (`temp_comp_control` @`0x289844), but the
  *device temperatures* and the flash calibration entries need the device; the
  backup→user conversion needs the origin calibration params from flash.
- **The backup/resume blob** (`obscan_device_management_backup_device_param`)
  still dispatches into libOrbbecSDK (vtable+0x200, per §17.4): settle it with
  the **libOrbbecSDK_arm64.dylib** binary (already in `.recon-cache/`) or a
  runtime hexdump of the blob — not from this snapshot. The wire did not show
  it either (it never left libOrbbecSDK), so the schema half of OQ-S15d stays
  open `[inferred, wire-observed 2026-10-18]`.

## 5. `obscan_calib_get_calibration_score` — score only

@`0x13ca8`: NULL handle → log_error + `0x32620107`; else it calls
`OrbbecCalib::get_calibration_score` @`0xbdd848` with the handle and **one**
caller-supplied `float*` — the native's other eight `float*` outputs are
passed NULL (`0x13ce0`–`0x13d08`). The C ABI therefore carries a single score;
no parameters. The calib family (`create` @`0x13604`, `start` @`0x13a34`, QR
@`0x13b48`, `save_data` @`0x13d40`, …) is the *user* calibration flow (board +
QR); the factory calibration (planes/intrinsics) does not flow through it.

## 6. Settled statically vs needs a run

**Settled (static):** the flash record format + keys; the plane write chain;
the live-copy sizes; the CRC-32 checksum recipe; the 41-field map's intrinsics
core; the thermal compensator's coefficients/algorithm/threshold and its
application points; the score export's shape.

**Settled (wire-observed, not a gated run):** the flash-read transport
(opcode 25 / `ob_rpc_send_data`, 768-B reads); the nine-record flash map; the
plane record at 0x211000 + the 0x213000 backup; the 5180-B payload length.

**Needs a run:** the payload semantics/units (plane coefficients, the unmapped
packed fields); the user struct's internal layout; whether the planes reach
the session dir; the compensator's numeric effect; the backup blob's schema;
the nine records' identities; checksum validation against the captured
bytes.

## OPEN QUESTIONS

- **OQ-S15a** — the 5120-B user plane struct's layout, the per-coefficient
  units, and the 60-byte flash-vs-live difference (5180 vs 5120) (a runtime
  hexdump of the cache struct, or a deeper consumer read).
- **OQ-S15b** — packed positions 1–3, 29–34, 35–41 semantics (a
  device-params hexdump alongside a session record).
- **OQ-S15c** — does the session dir carry a plane record (a session-dir
  listing from a laser scan)?
- **OQ-S15d** — the flash transport and the backup blob's schema. *Transport
  half: wire-observed 2026-10-18 (opcode 25 via `ob_rpc_send_data`, 768-B
  reads). Schema half: still open.*
- **OQ-S15e** — the 0.4-threshold units and the compensator's numeric effect
  (a temperature-step capture).
  *[ERRATUM 2026-10-19: superseded — threshold is 0.2°C; settled in `calibration-store-row95.md`]*
- **OQ-S15f** — the identities of the other flash record kinds
  (0x08/0x11/0x12/0x13/0x15) and the 0x20e000/0x215000 pairing.
