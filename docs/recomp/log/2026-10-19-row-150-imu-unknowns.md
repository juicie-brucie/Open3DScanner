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

- 2026-10-20 — recon (recon room, `wt-recon5`, branch `recon/row150-imu-unknowns`): **correction to the `row-150-imu-unknowns` entry above.** Five claims in it did not survive; they are **marked here, not deleted**. The rest of the entry stands.

  **Withdrawn — the invariant-axis cross-check.** The claim that the whole-run invariant-axis estimator "returns the same axis to 0.03", and that "the run's largest-variance direction is +ch1", are **withdrawn** — see findings **§10.1.2–§10.1.3**. The estimator's eigensolver had a sign error, and decisively the whole-run covariance's smallest-variance direction is *forced* into the plane the rotation axis already lies in, so the check could not have been independent. The axis conclusion does not rest on it: it rests on the PRE/POST means and the +69.94° rotation, which reproduce exactly. (The prior verifier's matrix was the **mean-centred** covariance — a *centering* difference, not a normalisation or subset one; and the raw second moment's largest eigenvector is (0.7052, 0.1907, 0.6829), **79.0°** from +ch1 — it is the *second* eigenvector (−0.2709, 0.9625, 0.0110) that is ≈ +ch1.)

  **Withdrawn — "33 zero-length device-timestamp gaps".** That figure exists only in the **sorted** timestamp sequence and is withdrawn; in the **delivered** order the device clock runs **backwards at 11 points** (11 negative spacings of −4 981 … −14 945 µs), because the 33 re-deliveries arrive in A B A B groups — findings §10.5.

  **Restored, with its rule — "191 samples are missing".** The figure is right again and the reason it was withdrawn was wrong. The rule: on the **sorted distinct** series a *gap* is a spacing **> 1.5 × the 4 982 µs median** and it hides **round(δ / 4 982) − 1** absent samples — **nearest-integer, not `floor()`**. That selects **35 gaps carrying 191 absent samples**, and **191 − 1.26 = 189.74 ≈ the exact 189.73** (the 1.26 is the accumulated 6 279 µs shortfall of the ~17 043 normal intervals, which average 4 981.63 µs — 0.37 µs under the 4 982 µs nominal). The intermediate "30 gaps / 168 absent / ~21 residual" was a `floor()` census, loses one on 23 of the 35, and is superseded — findings §10.5.

  **Superseded — the temperature's drift and noise.** "+62 counts in the first 15.6 s, flat to t ≈ 57 s, then −50 counts over the last 54 s" is superseded by **+70 counts over the first 20.7 s, peaking at 1318 at t = 46.081 s, then −56 counts over the last 65 s** (the run's last sample reads 1262 at t = 111.395 s); the successive-difference sd is **2.33** counts, not 1.56; and the correlation with |a| over the pick-up window is **−0.14 … −0.34** depending on the window (the whole-run −0.018 stands) — findings §10.2.

  **Corrected — the device-report header split.** The "66 … (the `02 00 10 01 …` header)" parenthetical is wrong: only **34** of the 87 `get_device_report_infomation` records carry that header; the other **32** are headerless stale ring residue, and **21** show an IMU header. The 66/21 split itself stands — findings §10.6.

  **Also corrected (findings §10.1, §10.1.4).** The per-step rotation axis wanders further than quoted: median consecutive change **34.4°**, p90 **126.7°**, max **179.8°**, with **99/499** steps within 26° of the end-to-end axis on the signed angle and **152/499** treating the axis as a line. In the POST pose gravity is **43.6°** from −ch0, **77.2°** from −ch1 and **49.2°** from −ch2 (not 46°/41°). Both conclusions — no channel-aligned pose, and "a summary of the net change, not a physical hinge" — are unaffected.

  **Tags:** every claim here remains **`[inferred]`** — a static measurement of a recorded capture, with no hardware run.
