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
