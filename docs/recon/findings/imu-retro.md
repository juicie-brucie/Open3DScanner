# IMU Retro Analysis — Row #150

*Retrospective analysis of the session4 IMU data (17,112 samples, 8,556 records). No device, no new capture, no vendor binary. This document consolidates the findings from §10 of `imu-stream.md` and specifies the operator captures required to settle the remaining open questions.*

## 1. What the Retro Pass Settles

### 1.1 Axis Order — Partially Settled

**Settled:**
- Gravity lies in the (ch1, ch2) plane, 11.82° from ch1
- ch0 is perpendicular to gravity to 0.55° in the PRE (pre-pickup) pose
- ch1 reads negative at rest (−8022 counts), so +ch1 points down (assuming standard MEMS convention)
- The pick-up re-oriented the device by 69.94° (a tip, not a yaw)
- The gravity magnitude is 8192 counts = 1 g at rest

**Not settled (OQ-150-1):**
- Which of ch0/ch2 is the device's X axis and which is Z
- The absolute handedness of the (ch0, ch1, ch2) triple relative to the world

**Why the capture cannot settle it:**
The capture contains no pose in which gravity lies exactly along a single channel. In the PRE pose, gravity is 11.82° from ch1 (not aligned with any channel). In the POST pose, gravity is 43.6° from −ch0, 77.2° from −ch1, and 49.2° from −ch2 — still not aligned. Without a gravity-aligned channel, the labeling of the horizontal pair (ch0, ch2) is a manufacturer convention the capture cannot see.

**Bounded inference from accelerometer sign:**
The capture shows that ch1 reads negative at rest. Under the standard MEMS convention (a stationary accelerometer reads +1 g on the axis that points up), this means the device's +ch1 axis points down. This is a bounded inference — it assumes the standard MEMS convention, which is nearly universal but not guaranteed.

**Minimal operator pose to settle it:**
Place the scanner in a known orientation where gravity is aligned with one of the horizontal channels (ch0 or ch2). For example:
- Lay the scanner on its side so that gravity is along ch0 (or ch2)
- Record the IMU stream
- The channel that reads ±8192 counts is the vertical axis in that pose
- Repeat with the scanner rotated 90° about the vertical to identify the other horizontal channel

Alternatively, rotate the scanner by a known angle (e.g., 90°) about a known axis (e.g., the table normal) while recording on video. The channel that stays constant is the rotation axis; the two channels that swap identify X/Z.

### 1.2 Temperature Unit — Not Settled

**Settled:**
- The temperature field is a live, slowly-varying analog channel (not a counter)
- Range: 1244–1318 counts over 86 s (5.8% swing)
- Sample-to-sample noise: σ = 2.33 counts
- Weak anti-correlation with |a| during motion (−0.14 to −0.34), no correlation at rest (−0.018)
- Macro shape: rises from 1248 to peak 1318 at t = 46.081 s, then decays to 1262 at t = 111.395 s

**Not settled (OQ-150-2):**
- The unit (°C, Kelvin, raw ADC counts, or something else)
- The zero point (offset)

**Plausible device models tested:**

| Scaling | Implied range | Verdict |
|---------|---------------|---------|
| ÷ 1 | 1244–1318 °C | Absurd (above solder melting point) |
| ÷ 10 | 124.4–131.8 °C | Above room temp; above most IMU max operating temps (BMI088: 85 °C, ICM-42688: 105 °C) |
| ÷ 16 | 77.75–82.38 °C | Above room temp; plausible for a hot die but unlikely for a room-temperature device |
| ÷ 64 | 19.44–20.59 °C | In room range (20–25 °C) ✓ |
| ÷ 62 | 20.06–21.26 °C | In room range ✓ |
| ÷ 100 | 12.44–13.18 °C | Below room temp; impossible for a powered chip |
| ÷ 100 with +10 °C offset | 22.44–23.18 °C | In room range ✓ |
| ÷ 1000 | 1.24–1.32 °C | Absurd |

**Excluded:**
- ÷ 1, ÷ 10, ÷ 100, ÷ 1000 (all absurd or impossible for a room-temperature device)

**Remaining:**
- ÷ 16 (hot die, unlikely but not impossible)
- ÷ 64, ÷ 62, ÷ 100 with offset (all in room range)

**The drift does not help:** The rise-then-fall shape is consistent with a self-heating die equilibrating after the stream starts, but it is equally consistent with the room's own drift. A pure exponential warm-up does not fall afterwards, so the observed decay is not a simple thermal time constant.

**What would settle it:**
One known-temperature soak:
1. Hold the device in a stable environment (e.g., 20 °C room)
2. Record the IMU stream for 5 minutes
3. Move the device to a different stable environment (e.g., 30 °C room, or a temperature chamber)
4. Record the IMU stream for 5 minutes
5. Compute the slope: Δcounts / ΔT = counts per °C
6. Compute the offset: counts at 0 °C

This single measurement gives both the scale and the zero, and it is the only way to distinguish "chip temperature" from "room temperature" from "supply rail".

### 1.3 Gyro — Confirmed Inert

**Settled:**
- The gyro slots are structurally present (cell offsets 8, 10, 12)
- The header byte 1 = 7 indicates 7 sensor channels (3 accel + 3 gyro + 1 temp)
- The gyro slots are zero in 17,112 / 17,112 samples (100%)
- The wire offers no knob to request a gyro: `ob_rpc_start_imu_stream` takes empty params
- The header has no varying mask (all 8 bytes are constant in every IMU record)

**Not settled (OQ-150-3):**
- Whether the device was asked for a gyro and did not deliver, or was never asked
- Whether the daemon's serializer writes zeros itself, or copies zeros from the device

**What the wire tells us:**
The IMU stream's configuration is entirely the daemon's. The client cannot request a gyro, cannot request a rate, and cannot select a channel set. The gyro is absent neither because a client disabled it (there is no client-supplied configuration) nor because a header mask cleared it (there is no varying mask). It is the daemon's / device's stream configuration.

**What would settle it:**
1. **Daemon serializer analysis** (REA of `libobsensor::pal::RPCServer::rawDataStreamCallback`): If it copies the gyro slots verbatim from a device buffer, the device did not send them; if it writes zeros itself, the daemon dropped them.
2. **Fast-rotation capture**: Rotate the scanner fast (e.g., 360°/s) while recording the IMU stream. If the gyro slots remain zero, they are genuinely inert; if they populate, the daemon was not requesting gyro data in the normal stream.
3. **SDK analysis**: The SDK creates `AccelSensor` / `GyroSensor` objects (noted in #138's logs). If there is a separate RPC method or stream type for gyro, it was not used in this session.

**Operator capture needed:**
Rotate the scanner fast (≥ 180°/s) about each of the three axes while recording the IMU stream. If the gyro slots remain zero, they are inert. If they populate, note the scaling and units.

### 1.4 Header — Partially Settled

**Settled:**
- Byte 0: record kind / format version (1 = IMU frame, 2 = device report)
- Byte 1: number of sensor channels in a cell (7 = 3 accel + 3 gyro + 1 temp)
- Byte 2: cell size (0x18 = 24 bytes for IMU, 0x10 = 16 bytes for device report)
- Byte 3: number of cells used in this record (2 for IMU, 1 for device report)

**Not settled (OQ-150-4):**
- Bytes 4–7: constant zero in every record of both kinds; meaning unknown

**What would settle it:**
Daemon serializer analysis. Bytes 4–7 may be reserved, unused, or a checksum that is always zero in this session.

### 1.5 Sample Loss and Record Kinds — Settled

**Settled:**
- 17,112 sample slots delivered
- 17,079 distinct device timestamps (33 repeats)
- 189.73 exact missing sample intervals over the 86.028 s span
- 35 real gaps (spacing > 7,473 µs) containing 191 absent samples
- 11 backwards clock steps (ring artifact signature)
- 11 anomalous records with cell grid at offset 24 (instead of 8) — overlapping window artifact
- 87 `get_device_report_infomation` records split three ways: 34 device-report header, 32 headerless residue, 21 showing an IMU header

**Not settled (OQ-150-6):**
- Whether the 191 missing samples are capture loss (TCP sequence gap) or daemon overwrite (ring buffer overrun)

**What would settle it:**
TCP sequence analysis: If the same gaps are absent from the pcap's TCP sequence accounting, they are daemon overwrite; if the TCP sequence is continuous, they are capture loss.

**Operator capture needed:**
None — this is a post-capture analysis question. The pcap file must be examined for TCP sequence gaps.

## 2. What the Retro Pass Excludes

The retro pass cannot settle:
1. **X/Z axis labeling and absolute handedness** (OQ-150-1) — no gravity-aligned channel in the capture
2. **Temperature unit and zero** (OQ-150-2) — no known-temperature reference
3. **Why the gyro is inert** (OQ-150-3) — no daemon serializer analysis, no fast-rotation capture
4. **Header bytes 4–7** (OQ-150-4) — constant zero, no variation
5. **Device-report record cell layout** (OQ-150-5) — different record kind, not decoded
6. **Capture loss vs daemon overwrite** (OQ-150-6) — requires TCP sequence analysis

## 3. Operator Captures Still Needed

### 3.1 X/Z Axis Labeling (OQ-150-1)

**Capture:**
- Place the scanner in a known orientation (e.g., upright, on its side, upside-down)
- Record the IMU stream for 10 seconds
- Repeat for at least 3 orientations where gravity is aligned with different channels
- Optionally: record on video to document the orientation

**Analysis:**
- Identify the channel that reads ±8192 counts when aligned with gravity
- Map the channels to X/Y/Z based on the device's physical orientation

### 3.2 Temperature Calibration (OQ-150-2)

**Capture:**
- Hold the device in a stable environment (e.g., 20 °C room)
- Record the IMU stream for 5 minutes
- Move the device to a different stable environment (e.g., 30 °C room, or a temperature chamber)
- Record the IMU stream for 5 minutes
- Optionally: use a temperature chamber to sweep from 10 °C to 40 °C

**Analysis:**
- Compute the slope: Δcounts / ΔT = counts per °C
- Compute the offset: counts at 0 °C
- Verify linearity

### 3.3 Gyro Population (OQ-150-3)

**Capture:**
- Rotate the scanner fast (≥ 180°/s) about each of the three axes (X, Y, Z)
- Record the IMU stream during rotation
- Hold the scanner still for 10 seconds between rotations

**Analysis:**
- Check if the gyro slots populate during rotation
- If they populate, compute the scaling (counts per °/s) and units
- If they remain zero, the gyro is genuinely inert in this stream

### 3.4 Three-Mode / Pause / Slider Sitting (#145/#150)

**Capture:**
- Record the IMU stream during three modes:
  1. **Three-mode**: Rotate the scanner through a sequence of orientations (e.g., upright, on side, upside-down, on other side)
  2. **Pause**: Hold the scanner still for 30 seconds, then move it, then hold still again
  3. **Slider sitting**: Place the scanner on a slider (or rotate it slowly about the vertical axis)

**Analysis:**
- Test the motion gating logic (|a| − 8192 and gravity-direction delta)
- Verify the temperature field behavior during motion vs rest
- Check if the gyro slots populate during rotation

### 3.5 IMU Soak (Long-Term Stability)

**Capture:**
- Record the IMU stream for 1 hour with the scanner stationary
- Optionally: vary the room temperature during the soak

**Analysis:**
- Check for long-term drift in the temperature field
- Verify the accelerometer stability (|a| should remain 8192 ± noise)
- Check for sample loss or gaps over the long term

## 4. Summary

**Settled by the retro pass:**
- Gravity lies in the (ch1, ch2) plane, 11.82° from ch1
- ch0 is perpendicular to gravity to 0.55° in the PRE pose
- +ch1 points down (assuming standard MEMS convention)
- The pick-up re-oriented the device by 69.94° (a tip, not a yaw)
- The temperature field is a live, slowly-varying analog channel (not a counter)
- The temperature unit is not settled; plausible scalings are ÷ 16, ÷ 64, ÷ 62, or ÷ 100 with offset
- The gyro slots are structurally present and zero in 100% of samples
- The wire offers no knob to request a gyro
- The header bytes 0, 1, 2, 3 are named
- 17,112 sample slots delivered, 189.73 exact missing samples, 35 real gaps containing 191 absent samples
- 11 backwards clock steps (ring artifact signature)
- 11 anomalous records with overlapping window artifact

**Not settled (requires operator captures or daemon analysis):**
- X/Z axis labeling and absolute handedness (OQ-150-1)
- Temperature unit and zero (OQ-150-2)
- Why the gyro is inert (OQ-150-3)
- Header bytes 4–7 (OQ-150-4)
- Device-report record cell layout (OQ-150-5)
- Capture loss vs daemon overwrite (OQ-150-6)

**Operator captures needed:**
1. X/Z axis labeling: 3+ orientations with gravity aligned to different channels
2. Temperature calibration: known-temperature soak (20 °C → 30 °C)
3. Gyro population: fast rotation (≥ 180°/s) about each axis
4. Three-mode / pause / slider sitting: motion gating and temperature behavior
5. IMU soak: long-term stability (1 hour stationary)

---

**Wall**: `docs/recon/**`
**Status**: `[inferred]` — retrospective analysis of existing session4 data, no new capture
**Supersedes**: None — this document consolidates §10 of `imu-stream.md` and specifies operator captures
**Related**: #138 (original IMU decode), #150 (retro analysis in `imu-stream.md` §10), #145 (three-mode/pause/slider captures)
