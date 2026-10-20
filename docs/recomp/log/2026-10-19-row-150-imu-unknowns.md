- 2026-10-19 — recon (recon room, `wt-recon5`, branch `recon/row150-imu-unknowns`): **#150 — the IMU record's remaining unknowns, settled as far
  as the bytes allow.** Capture-only, no device: `session4/small_payloads.tsv` re-decoded with the capture set's own `msgpack_decode.py`; the
  **17 112 genuine samples** were re-derived, matching #138 exactly.

  **Axis order** — **`ch1` is the vertical axis** (the whole 8192-count gravity vector lies in the (ch1, ch2) plane; the device's vertical is
  11.82° off `ch1`) **and `ch0`/`ch2` are the horizontal pair**: the pick-up's rotation axis is perpendicular to gravity (a tip, not a yaw) and lies
  in the (ch0, ch2) plane to within **9.46°** — which is that 11.82° tilt. The invariant-axis estimator over the whole run returns the same axis
  to 0.03, and the run's largest-variance direction is +ch1. Rotation sense: **+69.94° about (0.633, 0.164, −0.756)**; `ch1` reads negative at
  rest, so **+ch1 points down**. **Not settleable from the capture:** which of ch0/ch2 is X, and the absolute handedness (no gyro, no world
  reference) — **OQ-150-1**.

  **Temperature** — the unit is **not** settled and the bytes cannot settle it: no offset-free scaling lands in a 20–25 °C room (÷1 → 1244 °C,
  ÷10 → 124.4 °C, ÷100 → 12.4 °C, ÷1000 → 1.24 °C), so it is **raw counts**; ÷64, ÷100-with-a-+10 °C-offset and ÷62 all would land in the room.
  It is a live analog channel (per sample, successive-difference sd 1.56, range 1244…1318, **corr with |a| = −0.018** so not an acceleration
  artefact), drifting **+62 counts in the first 15.6 s, flat to t ≈ 57 s, then −50 counts over the last 54 s** — **OQ-150-2**.

  **Gyro** — the wire offers **no knob**: `ob_rpc_start_imu_stream0-2-16` is sent **once with empty params**, exactly like `get_imu_frame`; the
  header has **no varying mask**; the slots are structurally present (byte 1 = 7 = the cell's 7 channels, and the cell is exactly 12 u16 =
  index + 7 + a 4-u16 timestamp) and zero in 17 112/17 112 — **OQ-150-3**.

  **Header** — bytes **0, 2 and 3 are named**, because a **second header value exists in the same capture** (`02 00 10 01 00 00 00 00`, in 34 of
  the 87 device-report records): byte 0 = record kind/version, byte 2 = cell size (24 and 16; both divide 248 − 8 = 240 exactly), byte 3 = cells
  used (2 and 1). Byte 1 = 7 = the cell's channel count; bytes 4–7 constant zero. **No rate field, no channel-mask field** — **OQ-150-4**.

  **The 11 anomalous records** — an **overlapping window, not a short read**: the cell grid starts at offset 24 in exactly those 11, the header
  appears twice with non-zero residue between, all 11 are 248 B and decode to two valid samples, and they sit exactly on the run's **33
  zero-length device-timestamp gaps** ⇒ the payload is a **248-byte window into a byte ring** whose write pointer was off a record boundary.
  Separately **191 samples are missing** over the 86 s (capture loss vs daemon overwrite — **OQ-150-6**).

  **Correction to §1:** `get_device_report_infomation` does **not** always return an IMU frame — 66 of its 87 records carry a **different**
  248-byte record, so it is a different record kind (**OQ-150-5**).

  **Row → `SPEC-READY`, not `DONE`** — items (a), (d) and (e) are settled or bounded; the temperature unit, the X/Z naming and handedness, the
  gyro's cause, the device-report cell and the sample-loss cause each keep a named next evidence in findings §10.7.
