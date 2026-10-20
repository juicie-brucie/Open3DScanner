# IMU stream — the 248-byte record on port 10011

Row #138. Decoded from the protocol capture only (`~/DevReposiMac/raptor-protocol-capture/session4/`);
no device, no new capture, no vendor headers, no `.recon-cache` consulted. The capture was read
read-only; all decode scripts ran in a scratch directory and are not part of this repo.

**Status: the record layout is decoded and the accelerometer is validated against gravity.**
The gyro slots are present in the record but carry no data, and the axis *order* and the
temperature scale are **not** settled — see OPEN QUESTIONS.

---

## 1. What is actually on the wire (and a correction to the row's premise)

The capture's port 10011 traffic was decoded with the capture set's own `msgpack_decode.py`.
Every port-10011 request/response pair was correlated by msgpack `msgid`, so each response is
attributed to the method that asked for it:

| method (port 10011) | calls | empty replies | replies with a 248-byte payload |
| --- | --- | --- | --- |
| `ob_rpc_start_imu_stream0-2-16` | 1 | — | returns `true` |
| `ob_rpc_get_imu_frame0-2-16` | **15 892** | 7 336 | **8 556** |
| `ob_rpc_get_device_report_infomation0-2-16` | 671 | 584 | 87 |
| `ob_rpc_stop_imu_stream0-2-16` | 1 | — | returns `nil` |

**Correction to the row's premise.** The 8 643 × 248-byte records are **not** all
`get_device_report_infomation` replies. **8 556 of them are `ob_rpc_get_imu_frame0-2-16` replies**
(the remaining 87 are the device-report method). The device-report method was called only 671
times in the whole capture — it cannot have produced 8 643 records. The row's *data* (8 643
records, 248 bytes each, ~100 Hz, t = 25.37 → 111.40 s) is right; its *attribution* was not.

Both methods return the **same 248-byte payload** and it carries the same cell array (§3), so
the device-report records are a second view of the same buffer, not a different format.

Both methods' replies are a msgpack `bin8` of 248 bytes inside a **256-byte** msgpack frame
(`91 01 <msgid:4> 91 c0 c4 f8 <248 B>` = 8 + 248). **[inferred]** — 8 483 of the IMU replies are
exactly 256 B on the wire; 66 are 255 B and 7 are 254 B (msgpack `msgid`/envelope padding
variations).

**Correction to `protocol-spec.md` §1.2 / §6.6.** That file records `ob_rpc_get_imu_frame` as
returning "a 504-byte sample string" with "float64-looking timestamps (~1.87e9) and float
samples, zero-padded". **No 504-byte reply exists anywhere in this capture**, and the record is
neither float64-timestamped nor float-valued: it is 248 bytes, the timestamp is a **uint64**
little-endian microsecond counter, and the sensor channels are **int16**. The 504-byte figure is
not reproduced and should not be carried forward. (`protocol-spec.md` §1.1's *call counts* for
this capture — `get_imu_frame` 15 892, `get_device_report_infomation` 671 — are correct.)

---

## 2. Is it one sample or a batch? — a batch of **two**

248 = 8 + 10 × 24. The payload is an 8-byte block header followed by a grid of **24-byte cells**:

* **8 545 of 8 556** IMU records have **exactly 2 non-zero cells** (the two cells at offsets 8
  and 32); the remaining 8 cells (offsets 56 … 247) are **entirely zero**.
* **11** records have a **third** cell that is not entirely zero (bytes 62–68) — see §6.

So the record carries **2 samples per record**, not one. The cell grid is a fixed 10-slot array;
the daemon fills what it has and zero-pads the rest. **A record's sample count is therefore not
in the header** — it is implied by which cells are non-zero.

---

## 3. The layout

All multi-byte fields are **little-endian**.

### 3.1 Block header — 8 bytes at the payload start

```
01 07 18 02 00 00 00 00
```

Constant in every IMU record and every device-report record in this capture.
Candidate reading **[inferred]**: `u8 version = 1`, `u8 ? = 7`, `u8 cell_size = 24` (0x18),
`u8 cell_count = 2`, then 4 reserved zero bytes. The two values that *do* match the observed
structure are `0x18 = 24` (the cell size) and `0x02 = 2` (the cells used). The meaning of `0x01`
and `0x07` is unknown.

### 3.2 Cell — 24 bytes, at offsets 8, 32, 56, … (relative offsets below)

| rel | size | type | field | observed |
| --- | --- | --- | --- | --- |
| 0 | 2 | `u16` | `sample_index` | 0 for the first cell of a record, 1 for the second (always) |
| 2 | 2 | `i16` | `accel_0` | −7 223 … +2 846 |
| 4 | 2 | `i16` | `accel_1` | −12 400 … +426 — **the gravity axis at rest** |
| 6 | 2 | `i16` | `accel_2` | −8 589 … +1 306 |
| 8 | 2 | `i16` | `gyro_0` | **0 in all 17 112 samples** |
| 10 | 2 | `i16` | `gyro_1` | **0 in all 17 112 samples** |
| 12 | 2 | `i16` | `gyro_2` | **0 in all 17 112 samples** |
| 14 | 2 | `u16` | `temperature` | 1 244 … 1 318, drifts slowly |
| 16 | 8 | `u64` | `timestamp` | device clock, **microseconds** since boot |

The three `accel_*` fields are the only channels that carry sensor data. The three `gyro_*`
slots exist in the frame but are **never non-zero** — the device is not putting angular rate on
the wire in this stream. Whether they are "reserved", "gyro present but zero", or "gyro
deliberately disabled by the daemon" is not settled (§8).

### 3.3 Scale

**`accel` is int16 with 8192 counts = 1 g.** Derived, not assumed: during a still window the
three-component vector has magnitude exactly 8192 (see §5). Concretely, over t = 108–111 s
(680 samples) the mean vector is `(98.5, −8042.5, −1550.9)`, whose magnitude is **8191.2**, and
the per-second magnitude means are 8192.1 (t = 50 s) and 8188.9 (t = 108 s). 8192 = 2¹³ is a
clean full-scale marker and is consistent at two independent still orientations (§5).

Per-sample noise in a still window is σ ≈ 56 / 102 / 99 counts on the three axes (≈ 1 % of 1 g).

### 3.4 Timestamp

`u64` **microseconds** on the device's own clock. Evidence:

* consecutive samples inside a record differ by **4 980–4 984 µs** (median 4 982), and the
  device-clock advance per record (9 962 µs) matches the wall-clock advance per record
  (10.05 ms) to within 1 %;
* the device clock is offset from the capture clock by a constant **+6 143.476 s**
  (ts 6 251 483 474 µs at capture t = 108.007408 s);
* the counter is monotonic across the whole 86 s with no wrap.

---

## 4. Rate

| quantity | value |
| --- | --- |
| `get_imu_frame` poll rate | 15 892 / 86.03 s = **184.7 /s** |
| records carrying data | 8 556 / 86.02 s = **99.5 records/s** (≈ 100 Hz) |
| **samples** | 17 112 / 86.03 s = **198.9 samples/s** (≈ 200 Hz) |
| sample spacing (device clock) | **4 982 µs** |

⇒ **The rate is 100 Hz per record and 200 Hz per sample** — the record is a *batch of two*
samples, so the work order's "100 Hz per record or per sample" question resolves to **both**:
100 records/s, 200 samples/s.

Data flows from t = 25.373 s to t = 111.395 s of the capture (86.03 s). Before that, the polls
return empty; after it, the stream stops (`ob_rpc_stop_imu_stream` at t = 121.13 s).

---

## 5. Validation against motion

The capture's own motion phases are not documented (it is the CrealityScan app's session, not the
#112 run), so the validation is **self-referential**: the accelerometer must read 1 g when the
device is still and must leave the 1 g sphere when it is moved. Both are observed, and the
transition is a single clean event.

**Still (t = 27.00–27.50 s):** mean vector `(80, −8022, −1675)`, |a| = 8 196 / 8 196 / 8 191 —
within **0.05 %** of 8192. The per-second gravity-direction change is **0.08°**.

**Motion event (t = 27.75–29.75 s):** the scanner is picked up and re-oriented.

| t (s) | mean accel | \|a\| | dev. from 1 g |
| --- | --- | --- | --- |
| 27.50 | (80, −8017, −1679) | 8191.4 | −0.01 % |
| 27.75 | (78, −8083, −2081) | 8347.3 | **+1.90 %** |
| 28.00 | (−860, −7821, −4971) | **9306.9** | **+13.61 %** |
| 28.25 | (−3312, −5274, −5181) | 8100.8 | −1.11 % |
| 28.50 | (−4791, −3615, −5046) | 7841.0 | −4.28 % |
| 28.75 | (−5906, −2267, −4484) | 7754.1 | −5.35 % |
| 29.00 | (−6247, −1683, −4407) | 7828.0 | −4.44 % |
| 29.50 | (−5487, −1679, −5842) | 8189.1 | −0.04 % |
| 30.00 | (−5596, −1851, −5773) | 8250.5 | +0.71 % |

This is the textbook signature of a hand-held re-orientation: the **magnitude** leaves the 1 g
sphere while the device is accelerating (+13.6 % at the peak, i.e. linear acceleration
superimposed on gravity), the **direction** rotates smoothly through ≈ 49° in one second and
≈ 19° in the next, and the magnitude returns to 8192 (−0.04 %) once the scanner is set down in
its new orientation. The per-second direction change over the rest of the capture is 0.1–4°.

**What this does *not* validate:** the gyro slots (they are zero), the axis order, and the
temperature scale.

---

## 6. The 11 anomalous records (0.13 %)

11 of the 8 556 IMU records carry an **extra 8 bytes** at offset 8 and a **second copy of the
block header at offset 16**; the cell grid is then shifted by +16 and the cells read correctly
from offset 24. The same shift, with the header at offset 232, is visible in one
`get_device_report_infomation` record — whose cells then *wrap* around the end of the 248-byte
payload (a cell starting at 240 continues at 0), and read correctly across the wrap.

**Interpretation [inferred]:** the 248-byte payload is a **circular buffer** — an 8-byte header
plus a 24-byte-cell grid — and the reader is handed a 248-byte window that may begin at any
cell boundary. 248 = 8 + 10 × 24 exactly, so the grid wraps on a cell boundary. In the common
case the header sits at offset 0; in the 11 odd records it sits at offset 16.

---

## 7. What a consumer would get

* **A 200 Hz accelerometer stream with a device-clock timestamp**, in a fixed 248-byte frame
  that needs no reassembly beyond taking cells 0 and 1.
* Gravity is directly observable (magnitude 8192 = 1 g), so **motion gating** ("is the scanner
  still or moving?") is a one-line test on |a| − 8192 and on the gravity-direction delta. The
  capture shows a clean 0.05 % still floor and a +13.6 % peak during a hand move.
* **No angular rate.** The three gyro slots are zero throughout, so **pose stabilisation,
  motion gating by rotation, and gyro-assisted tracking recovery cannot be built from this
  stream** as captured — the accelerometer alone gives translation and gravity direction, not
  rotation about the gravity axis.

---

## 8. OPEN QUESTIONS

*§10 (added by row #150) settles or narrows items 1, 4 and 5 of this list and
bounds item 3; see §10.7 for the row's own open questions.*

1. **Axis order — not settled.** The three int16 channels are fixed in order and the
   *gravity-dominant* one is the **second** (`accel_1`), but which of them is X / Y / Z in the
   device frame is not derivable from this capture: it contains no gyro, no known rotation, and
   no second orientation reference. **Needs:** a controlled capture where the scanner is rotated
   about a *known* axis (e.g. 90° about the table normal) — then the channel that stays
   constant is the rotation axis and the two that swap identify X/Z.
2. **Gyro slots.** Zero in 17 112/17 112 samples. Whether the daemon can be asked for a populated
   gyro (a different stream type, a property, or the `AccelSensor`/`GyroSensor` objects the SDK
   logs creating) is **not** answerable from the capture. **Needs:** the vendor API surface
   (row #138's second half) or a capture of a session where the scanner is rotated *fast*, to
   confirm the slots are genuinely inert rather than merely zero at 200 Hz.
3. **Temperature unit.** The field drifts 1 244 → 1 318 over 86 s (up 1.3 %/10 s to a peak at
   t ≈ 42 s, then slowly down). `/100` gives 12.4–13.2 (a °C reading for a room-temperature
   device); `/1000` gives 1.24–1.32 (a voltage or coefficient). The capture cannot separate
   them. **Needs:** the device's own temperature report, or a known-temperature soak.
4. **The header's bytes.** `0x18 = 24` (cell size) and `0x02 = 2` (cells used) fit; `0x01` and
   `0x07` are unassigned. **Needs:** the daemon's serializer (the same REA lane as row #131,
   §10 of `frame-codec.md`).
5. **The ring-buffer offset** (§6) is an inference from 11 records; the daemon's writer would
   confirm it.
6. **Whether the device-report method carries anything beyond the same cell array** — its 87
   non-empty replies in this capture decode to the same cells (a valid µs timestamp, the same
   temperature field, the same accel channels) and nothing more; there may be device fields in
   other modes. **Needs:** the daemon's writer.

---

## 9. Reproduction

```python
# read-only; msgpack decoder from the capture set
import sys; sys.path.insert(0, "/Users/brucemackay/DevReposiMac/raptor-protocol-capture")
from msgpack_decode import unpack
# for each port-10011 msgpack frame:
#   request  [0, msgid, method, params]  -> remember msgid -> method
#   response [1, msgid, nil, result]     -> result is bytes (bin8)
# for a 248-byte imu_frame payload p:
#   header = p[0:8]                       # 01 07 18 02 00 00 00 00
#   for cell in (p[8:32], p[32:56]):
#       idx, a0, a1, a2, g0, g1, g2, temp = struct.unpack_from('<8H', cell, 0)
#       a0, a1, a2, g0, g1, g2 = struct.unpack_from('<6h', cell, 2)
#       ts_us = struct.unpack_from('<Q', cell, 16)[0]
#       # |a| == 8192 counts == 1 g while still
```

Raw evidence: `session4/small_payloads.tsv` (354 430 lines; 33 130 on port 10011), capture
`session4/session4.pcapng`. The capture directory is read-only and was not modified.

---

## 10. The remaining unknowns, settled as far as the bytes allow (row #150)

*Added 2026-10-19, row #150 (recon room). **Capture-only, no device**: the same
`session4/small_payloads.tsv` / `session4.pcapng` that row #138 read, decoded again
with the capture set's own `msgpack_decode.py`. No vendor headers, no
`.recon-cache`, no new capture. Every claim below is **`[inferred]`** from static
measurement of the capture — there is no hardware run in this row, so no `[hw-*]`
tag applies. §1–§9 are unchanged; this section only adds to them.*

### 10.0 The corpus, re-derived

| port-10011 request | calls | params |
| --- | --- | --- |
| `ob_rpc_start_imu_stream0-2-16` | 1 | **empty** (`90` = fixarray(0)) |
| `ob_rpc_get_imu_frame0-2-16` | 15 892 | empty |
| `ob_rpc_get_device_report_infomation0-2-16` | 671 | empty |
| `ob_rpc_stop_imu_stream0-2-16` | 1 | **empty** |

16 563 replies carry a binary payload; **8 643** are 248 B and **7 920** are empty.
Of the 8 643, **8 556** are `get_imu_frame` and **87** are
`get_device_report_infomation`. Decoding the cells yields **17 112 genuine IMU
samples** (8 556 records × 2) — the same count row #138 measured, so the decode
agrees.

**The cell-grid rule, re-derived independently** (grid start = the offset just
after a valid header; 24-byte cells, wrapping at 248): the grid starts at
**offset 8 in 8 545 records** and at **offset 24 in exactly 11**. Every one of the
8 556 records decodes cleanly under that rule — no record fails.

---

### 10.1 Item 1 — axis order: the vertical axis and the horizontal pair are settled; the X/Z naming and the absolute handedness are not

**Method.** Mean acceleration over a still window before the pick-up and one after;
the rotation that carries the first mean direction to the second; and an
independent cross-check with the **invariant-axis estimator** (the eigenvector of
the sample covariance with the smallest eigenvalue — the axis along which the
acceleration vector does not change).

| quantity | value |
| --- | --- |
| PRE still (t = 26.00–27.50 s, n = 300) mean | (78.5, −8021.9, −1676.8), \|a\| = **8195.6** |
| PRE still, unit direction | (0.0096, −0.9788, −0.2046) |
| POST still (t = 29.75–31.50 s, n = 352) mean | (−5899.7, −1799.9, −5328.7), \|a\| = **8151.1** |
| POST still, unit direction | (−0.7238, −0.2208, −0.6537) |
| **net rotation of the gravity direction in device coordinates** | **69.94°** |
| **rotation axis, in the device's channel frame** | **(0.6331, 0.1643, −0.7564)** |
| per-channel mean change, PRE → POST | ch0 −5978.2, ch1 +6222.0, ch2 −3651.9 |
| the device's vertical axis vs `ch1` | **11.82° apart** |
| the rotation axis vs the (ch0, ch2) plane | **9.46°** |
| invariant-axis estimator, whole run | (0.655, 0.193, −0.730) — **the same axis to 0.03** |
| second eigenvector of the run's covariance | (−0.271, +0.963, +0.011) ≈ **+ch1** |

**What is settled.**

1. **`ch1` is the vertical (gravity) axis.** At rest the whole of the 8192-count
   gravity vector lies in the (ch1, ch2) plane: ch1 = −8021.9 = 8192·cos 11.8°,
   ch2 = −1676.8 = 8192·sin 11.8°, and ch0 ≈ 0. The scanner was **not exactly
   upright** before the pick-up — it was tilted 11.8° away from `ch1`.
2. **`ch0` and `ch2` are the horizontal pair.** The pick-up's rotation axis is
   perpendicular to gravity by construction (a *tip* about a horizontal axis, not
   a yaw — a yaw would not move the gravity direction at all), and that axis lies
   in the (ch0, ch2) plane to within **9.5°**, which is the 11.8° tilt of the
   device's own vertical away from `ch1`. The two numbers agree, so this is not
   a coincidence. The invariant-axis estimator over the whole 86 s returns the
   same axis independently, and the run's *largest*-variance direction is +ch1 —
   the gravity direction, as it must be.
3. **The sign on the vertical channel.** `ch1` reads **negative** at rest
   (−8021.9). Under the standard MEMS convention — a stationary accelerometer
   reads +1 g on the axis that points **up** — the device's **+ch1 points down**,
   so the scanner's "up" direction is **−ch1**. *[inferred: the convention is
   assumed, not measured.]*
4. **The rotation sense.** The gravity vector, expressed in device coordinates,
   rotates from (0.0096, −0.9788, −0.2046) to (−0.7238, −0.2208, −0.6537): a
   **+69.94°** rotation about the device-frame axis (0.633, 0.164, −0.756) in the
   right-hand sense of the channel frame (equivalently, the device's own rotation
   is the inverse rotation about the same axis). The path is not monotone: the
   cumulative direction change per 0.25 s runs 0.3° before t = 27.75 s, then
   11.1 + 21.9 + 21.5 + 11.9 + 11.4° through t = 28.88 s (≈78°, an overshoot),
   settling back to the net 69.9° by t ≈ 29.6 s, with ±1–3° of jitter after.

**What the bytes cannot settle.** Which of `ch0` / `ch2` is the device's **X**
and which is its **Z**, and the **absolute handedness** of the (ch0, ch1, ch2)
triple relative to the world. An accelerometer yields three numbers; with no gyro
and no absolute world reference in the capture, the *labelling* of the horizontal
pair is a manufacturer convention the capture cannot see. **Needs:** a controlled
capture in which the scanner is rotated by a *known* angle about a *known* axis
(e.g. 90° about the table normal, recorded on video) **plus** the device's own
axis convention. → **OQ-150-1.**

---

### 10.2 Item 2 — the temperature field: a live analog channel, unit **not** settled

**What was measured.**

| property | value |
| --- | --- |
| range over the run | **1244 … 1318** counts (a 5.8 % swing) |
| sampled per | **sample**, not per record — the two cells of a record differ in 5 496/8 556 records (by ±2, ±4, ±6 counts) and are equal in 3 060 |
| sample-to-sample noise | sd of successive differences = **1.56 counts** |
| correlation with \|a\| (all 17 112 samples) | **−0.018** |
| correlation with \|a\| (pick-up window) | **−0.136** |
| macro shape | 1248 at t = 25.37 s → 1304 at t = 39 s → **peak 1310 at t ≈ 41 s** → plateau 1298–1310 to t ≈ 57 s → steady decline to **1260 at t = 111.4 s** |

So the field is a **live, slowly-varying analog channel**, not a counter, and it
**does not track motion** (|corr| ≤ 0.02 over the whole run) — so it is not an
acceleration-coupled artefact such as a supply-rail droop.

**The unit is not settled, and no offset-free scaling works.** Against a room at
20–25 °C:

| scaling | implied reading | verdict |
| --- | --- | --- |
| ÷ 1 | 1244 … 1318 °C | absurd |
| ÷ 10 | 124.4 … 131.8 °C | far above a room, and above every common IMU die's maximum operating temperature (BMI088 85 °C, ICM-42688 105 °C) |
| ÷ 100 | 12.44 … 13.18 °C | below the room — impossible for a powered chip |
| ÷ 1000 | 1.24 … 1.32 °C | absurd |

⇒ **The field is not degrees Celsius in any standard scaling; it is raw counts**
(or a scaled value with an unknown scale and/or an unknown offset). Several
scalings *would* land in a 20–25 °C room — 1/64 (19.4 … 20.6 °C), 1/100 with a
+10 °C offset (22.4 … 23.2 °C), ≈1/62 (20.1 … 21.3 °C) — and the capture cannot
choose between them. The drift does not help either: **+62 counts in the first
15.6 s, then flat for ~16 s, then −50 counts over the last 54 s.** That shape is
consistent with a **self-heating die** equilibrating after the stream starts, but
it is equally consistent with the room's own drift, and a pure exponential
warm-up does not fall afterwards.

**What would settle it.** One **known-temperature soak**: hold the device in a
stable environment, record the field, change the temperature by a known ΔT and
read the slope. That single measurement gives both the scale (counts/°C) and the
zero — and it is the only way to tell "chip temperature" from "room temperature"
from "supply rail". → **OQ-150-2.**

---

### 10.3 Item 3 — the gyro: the wire offers **no** knob, and no header mask

1. **`ob_rpc_start_imu_stream0-2-16` is sent once with empty params.** It is
   exactly like `ob_rpc_get_frame` and `ob_rpc_get_imu_frame` (both
   `enforce_arg_count(…, 0, …)` on the daemon's side — row #131 §10.2). ⇒ **a
   client cannot request a gyro, cannot request a rate, and cannot select a
   channel set.** The IMU stream's configuration is entirely the daemon's.
2. **The gyro slots are structurally present.** The header's byte 1 is **7**, and
   the IMU cell is exactly **12 u16 = index(1) + 7 channels + timestamp(4 u16) =
   24 B** — the 7 channels being 3 accel + 3 gyro + 1 temperature. The gyro
   occupies cell offsets 8, 10 and 12.
3. **They are zero in 17 112 / 17 112 samples** (0.00 %).
4. **There is no channel mask in the header.** All eight header bytes are
   identical in every IMU record of the session, so no byte of it is an enable
   mask that could be observed switching.

⇒ **Verdict:** the gyro is absent **neither** because a client disabled it (there
is no client-supplied configuration) **nor** because a header mask cleared it
(there is no varying mask). It is the **daemon's / the device's** stream
configuration. Whether the device was asked for a gyro and did not deliver, or
was never asked, is not observable on the wire.

**What would settle it.** (a) The **daemon's serializer** — the REA lane of row
#131: `libobsensor::pal::RPCServer::rawDataStreamCallback` and the IMU source
port. If it copies the gyro slots verbatim out of a device buffer, the device
did not send them; if it writes zeros itself, the daemon dropped them. (b) A
capture of a session where the scanner is **rotated fast**, to confirm the slots
are genuinely inert rather than merely zero at 200 Hz. (c) The SDK's
`AccelSensor` / `GyroSensor` objects that #138 notes the SDK creating.
→ **OQ-150-3.**

---

### 10.4 Item 4 — the 8-byte header: bytes 0, 2 and 3 are now named

The IMU header is `01 07 18 02 00 00 00 00` in **every** IMU record — at offset 0
in 8 545 and at offset 16 in the 11 anomalous ones (§10.5). **A second header
value exists in the same capture**, in 34 of the 87 `get_device_report_infomation`
records:

```
IMU frame        01 07 18 02 00 00 00 00
device report    02 00 10 01 00 00 00 00
```

That second value is the control that separates the fields:

| byte | IMU frame | device report | reading |
| --- | --- | --- | --- |
| 0 | 1 | 2 | **record kind / format version** — 1 = IMU frame, 2 = device report |
| 1 | 7 | 0 | **the number of sensor channels in a cell** — the IMU cell is exactly 12 u16 = index(1) + **7 channels** + timestamp(4 u16); 3 accel + 3 gyro + 1 temperature. The device-report cell is metadata only. *[inferred — the arithmetic fits exactly, but there is only one non-zero observed value to check it against]* |
| 2 | 0x18 = 24 | 0x10 = 16 | **the cell size** — and both divide 248 − 8 = 240 exactly (240 = 24 × **10 cells** = 16 × **15 cells**) |
| 3 | 2 | 1 | **the number of cells used in this record** — the IMU record's cells 0 and 1 carry the two samples and cells 2…9 are zero; the device report carries one populated cell. It is *not* "the count of non-zero cells": in the 11 anomalous records the header still says 2 while a third cell is non-zero (§10.5) |
| 4–7 | 0 | 0 | reserved / unused — **constant in every record of both kinds, so unidentifiable from one session** |

**There is no rate field and no channel-mask field.** Nothing in the header
varies, so the header carries no rate, no mask and no enable byte: the rate
(200 Hz) is implied by the timestamps and the cell count, and the channel set is
implied by byte 1 and by the cell layout. → **OQ-150-4** (bytes 1 and 4–7 only).

---

### 10.5 Item 5 — the 11 anomalous records: an overlapping window, not a short read

Row #138 read the geometry correctly; this row measures it.

* **Exactly 11** of 8 556 IMU records (0.13 %) have their cell grid starting at
  **offset 24** instead of 8 — the same 11, and no others.
* In them the 8-byte header appears **twice** — at offset 0 and at offset 16 —
  and the 8 bytes at 8…15 are **non-zero cell-like residue**
  (`00 00 00 XX eY fZ fW eb`), not padding. ⇒ **the payload is not a
  self-contained record**: it is a **248-byte window that begins 16 bytes before
  the header that anchors its own cell grid**, carrying the tail of the previous
  window plus a second copy of the header.
* **They are not short reads and not corrupt.** All 11 are exactly 248 B; all
  decode to two valid samples (index 0 and 1, a device µs timestamp inside the
  run, a temperature in range); none fails the decode.
* **They duplicate samples.** The device-timestamp sequence over the run has
  **33 zero-length gaps** — the same timestamp delivered twice — and the
  anomalous records sit exactly on them. At t = 41.142 s the spacings around the
  record are `34873, 0, 4983, 0, 4981` µs; the same pattern holds for the other
  ten.
* ⇒ **Interpretation [inferred]:** the daemon's 248-byte payload is a **window
  into a byte ring**; when the ring's write pointer is not on a record boundary
  the window overlaps the previous one, and the client is served the same sample
  again with the previous header still in the window. This is a **framing
  artifact**, not data loss and not a short read.

**Separately, the run is not gap-free.** 191 samples are missing over the
86.028 s, in 36 gaps of 2, 3, 4, 5, 7, 9, 11, 15, 17, 19 and 21 sample
intervals; the nominal count in the span is 17 268 against 17 112 delivered
(33 of the delivered are the duplicates above). **Capture loss is the likely
cause** — the record rate is a steady 99.5/s and the client polls at 184.7/s, so
the daemon is never starved — but that is **not** proved from the payload alone.
**Discriminator:** the same gaps must be absent from the pcap's TCP sequence
accounting; if they are *not* capture loss, then the daemon's ring overwrote
samples and a consumer's 200 Hz stream has holes. → **OQ-150-6.**

---

### 10.6 A correction to §1

§1 records that "both methods return the **same** 248-byte payload and it
carries the same cell array". That is **wrong for 66 of the 87**
`get_device_report_infomation` records: those carry a **different 248-byte
record** whose header is `02 00 10 01 00 00 00 00` (cell size 16, one cell) —
a device report, not an IMU frame. The remaining 21 device-report records happen
to show an *IMU* header (16 of them at offset 16, 5 at offset 232) because they
are victims of the same windowing artifact as §10.5. The **device-report cell
layout is not decoded here** (it is not an IMU record and not in this row's
scope). → **OQ-150-5.**

---

### 10.7 OPEN QUESTIONS raised by this row

1. **OQ-150-1 — which of `ch0` / `ch2` is X, and the absolute handedness of the
   channel triple.** Not settleable from this capture: no gyro, no world
   reference. §10.1 settles the vertical axis (`ch1`, with +ch1 pointing down),
   the horizontal pair (`ch0`, `ch2`) and the rotation sense.
2. **OQ-150-2 — the temperature unit and the zero.** No offset-free scaling
   lands in a 20–25 °C room (§10.2), so the field is raw counts; a
   known-temperature soak settles it.
3. **OQ-150-3 — why the gyro is inert.** The wire has no knob and the header has
   no varying mask (§10.3); the daemon's serializer or a fast-rotation capture
   settles it.
4. **OQ-150-4 — header bytes 1 and 4–7.** Bytes 0, 2 and 3 are named (§10.4);
   byte 1's "channel count" reading is arithmetically exact but has one observed
   value; bytes 4–7 are constant zero.
5. **OQ-150-5 — the device-report record's 16-byte cell.** Not decoded (§10.6);
   it is a different record kind, not an IMU frame.
6. **OQ-150-6 — capture loss or daemon overwrite** for the 191 missing samples
   (§10.5).
