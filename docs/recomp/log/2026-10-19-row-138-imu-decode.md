# Row #138 - the 248-byte IMU record is decoded

- 2026-10-19 - **The 248-byte record on port 10011 is decoded**
  ([findings/imu-stream.md](../../recon/findings/imu-stream.md)), from the existing
  `~/DevReposiMac/raptor-protocol-capture/session4/` capture only - no device, no new
  capture.

  **Correction to the row's premise:** the 8,643 x 248-byte records are **8,556
  `ob_rpc_get_imu_frame0-2-16` replies** plus 87 `get_device_report_infomation` replies, not
  8,643 device-report replies - the device-report method was called only 671 times in the
  whole capture and cannot have produced them. The row's data was right; the attribution
  was not. Also: **`protocol-spec.md` §1.2/§6.6's "504-byte `get_imu_frame` sample with
  float64-looking timestamps" is not reproduced** - this capture has no 504-byte reply, the
  reply is 248 bytes in a 256-byte msgpack frame, the timestamp is a **uint64 µs** counter
  and the channels are **int16**, not floats.

  **The layout:** an 8-byte constant block header (`01 07 18 02 00 00 00 00`) followed by a
  grid of **24-byte cells** (248 = 8 + 10 x 24). Each cell is
  `{u16 sample_index, i16 accel_0, i16 accel_1, i16 accel_2, i16 gyro_0, i16 gyro_1,
  i16 gyro_2, u16 temperature, u64 timestamp_us}`. **Two cells are filled per record**, the
  other eight are zero.

  **The scale, derived not assumed:** the accelerometer is **8192 counts = 1 g** - in a still
  window the three-component vector has magnitude 8191-8192 at two independent
  orientations.

  **The rate:** 100 Hz per **record**, **200 Hz per sample** (2 samples/record; sample
  spacing 4,982 µs on the device clock, which tracks wall clock to 1 %).

  **Validated against motion:** still (t = 27.0-27.5 s) reads 8192 +/- 0.05 % with a
  0.08 deg/s gravity-direction change; a pick-up-and-re-orient at t = 27.75-29.75 s pushes
  |a| to **+13.6 %** at the peak and rotates the gravity direction by ~49 deg then ~19 deg,
  settling back to -0.04 %.

  **The gyro slots are empty** - zero in 17,112/17,112 samples. So this stream gives gravity
  and linear acceleration, **not** angular rate: motion gating is buildable, gyro-based pose
  stabilisation and rotation-based tracking recovery are not.

  **Still open (-> new row #150):** the **axis order** (which channel is X/Y/Z - the
  gravity-dominant one is the second, but X/Z is not derivable from this capture), the
  **temperature unit** (1,244-1,318), the header's `0x01`/`0x07` bytes, and whether the
  daemon can be asked for a populated gyro. #138 -> **SPEC-READY** (the row's deliverable
  named the axis order; the layout is decoded and the accelerometer validated, but the axis
  naming and the gyro question are not closed).
