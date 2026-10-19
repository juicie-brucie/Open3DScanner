# Replay Payloads from Vendor Capture (session4)

*Extracted verbatim from `/Users/brucemackay/DevReposiMac/raptor-protocol-capture/session4/small_payloads.tsv` — the 2026-10-18 capture digest of the vendor's CrealityScan client. `[inferred]` — wire observation only, no vendor binary.*

## 1. `start_stream` (2 calls, sequential IR → depth)

### IR (port 10013) at t=25.294078

**Full msgpack request frame** (TCP payload, client→server):
```
940004b96f625f7270635f73746172745f73747265616d302d322d3132950609cd0780cd04b032
```

**Decoded**: `[0, 4, "ob_rpc_start_stream0-2-12", [6, 9, 1920, 1200, 50]]`

**Params only**: `950609cd0780cd04b032` → `[6, 9, 1920, 1200, 50]`

### Depth (port 10010) at t=25.339820

**Full msgpack request frame**:
```
9400cca2b96f625f7270635f73746172745f73747265616d302d322d3130950709cd0780cd04b032
```

**Decoded**: `[0, <msgid>, "ob_rpc_start_stream0-2-10", [7, 9, 1920, 1200, 50]]`

**Params only**: `950709cd0780cd04b032` → `[7, 9, 1920, 1200, 50]`

**Difference from IR**: first param is `7` (depth stream type) vs `6` (IR stream type).

## 2. `ob_rpc_get_frame` (first 10 on each port)

### IR (port 10013) — all params `90` (empty array)

| idx | t       | full_hex |
|-----|---------|----------|
| 0   | 25.339738 | `940005b66f625f7270635f6765745f6672616d65302d322d313290` |
| 1   | 25.359966 | `940006b66f625f7270635f6765745f6672616d65302d322d313290` |
| 2   | 25.383764 | `940007b66f625f7270635f6765745f6672616d65302d322d313290` |
| 3   | 25.407811 | `940008b66f625f7270635f6765745f6672616d65302d322d313290` |
| 4   | 25.429719 | `94000db66f625f7270635f6765745f6672616d65302d322d313290` |
| 5   | 25.452156 | `94000eb66f625f7270635f6765745f6672616d65302d322d313290` |
| 6   | 25.477355 | `940015b66f625f7270635f6765745f6672616d65302d322d313290` |
| 7   | 25.489778 | `940017b66f625f7270635f6765745f6672616d65302d322d313290` |
| 8   | 25.492058 | `940018b66f625f7270635f6765745f6672616d65302d322d313290` |
| 9   | 25.515419 | `94001cb66f625f7270635f6765745f6672616d65302d322d313290` |

**Cadence**: 0.175681s for 9 intervals → avg 0.01952s ≈ 51.2 Hz (matches spec's 50 fps nominal).

### Depth (port 10010) — all params `90` (empty array)

| idx | t       | full_hex |
|-----|---------|----------|
| 0   | 25.358578 | `9400cca3b66f625f7270635f6765745f6672616d65302d322d313090` |
| 1   | 25.383191 | `9400ccc9b66f625f7270635f6765745f6672616d65302d322d313090` |
| 2   | 25.403694 | `9400cccab66f625f7270635f6765745f6672616d65302d322d313090` |
| 3   | 25.425962 | `9400cccdb66f625f7270635f6765745f6672616d65302d322d313090` |
| 4   | 25.447928 | `9400ccd0b66f625f7270635f6765745f6672616d65302d322d313090` |
| 5   | 25.448902 | `9400ccd1b66f625f7270635f6765745f6672616d65302d322d313090` |
| 6   | 25.449164 | `9400ccd2b66f625f7270635f6765745f6672616d65302d322d313090` |
| 7   | 25.449345 | `9400ccd3b66f625f7270635f6765745f6672616d65302d322d313090` |
| 8   | 25.471953 | `9400ccd6b66f625f7270635f6765745f6672616d65302d322d313090` |
| 9   | 25.472056 | `9400ccd7b66f625f7270635f6765745f6672616d65302d322d313090` |

## 3. `ob_set_ir_frame_light_type` (4 calls, before and after start_stream)

### IR (port 10013) — params `[6, 65535]`

- t=25.259529: `940001d9206f625f7365745f69725f6672616d655f6c696768745f74797065302d322d31329206cdffff`
- t=25.292844: `940003d9206f625f7365745f69725f6672616d655f6c696768745f74797065302d322d31329206cdffff`

### Depth (port 10010) — params `[7, 65535]`

- t=25.261083: `9400cc8cd9206f625f7365745f69725f6672616d655f6c696768745f74797065302d322d31309207cdffff`
- t=25.292971: `9400cca1d9206f625f7365745f69725f6672616d655f6c696768745f74797065302d322d31309207cdffff`

**Key observation**: first param = stream type (6=IR, 7=depth), second = 0xffff.

## 4. `SetProperty` via `send_data` (opcode 2) — up to first frame

### Before start_stream (t=25.281–25.283):

| t       | propid | value | vendor_frame_hex (first 40 chars) |
|---------|--------|-------|-------------------------------------|
| 25.281405 | 157    | 0     | `474d04000200af009d0000000000000040` |
| 25.282196 | 158    | 1     | `474d04000200b0009e0000000100000040` |
| 25.283024 | 2026   | 2403  | `474d04000200b100ea0700006309000040` |

### After start_stream (t=25.358–25.364, after first frame):

| t       | propid | value | vendor_frame_hex (first 40 chars) |
|---------|--------|-------|-------------------------------------|
| 25.358772 | 2022   | 8     | `474d04000200b300e60700000800000040` |
| 25.359506 | 2024   | 2     | `474d04000200b400e80700000200000040` |
| 25.360044 | 2020   | 1     | `474d04000200b500e40700000100000040` |
| 25.360723 | 3      | 0     | `474d04000200b600030000000000000040` |
| 25.362186 | 152    | 0     | `474d04000200b700980000000000000040` |
| 25.363587 | 153    | 0     | `474d04000200b800990000000000000040` |
| 25.364980 | 151    | 1     | `474d04000200b900970000000100000040` |

**Vendor frame structure**: `474d` (magic) + `size_hw` (4 hex) + `0200` (opcode 2) + `reqid` (4 hex) + `propid` (8 hex, u32 LE) + `value` (8 hex, i32 LE) + `expected_size` (2 hex).

## 5. First frame response timestamp

**t=25.339836** on port 10013 (IR), response head:
```
940105c09b000000ccffc40000c40000c400cea2c90618ff...
```

**Latency**: 25.339836 - 25.294078 = **0.045758s = 45.76 ms** after start_stream IR request.

(First depth frame response would follow depth start_stream at t=25.339820, but not captured in this extract — the TSV truncates large payloads.)

## 6. File composition and replay semantics

The companion `streaming-replay-payloads.hex` file is **grouped by call type, not chronologically**. It contains 36 frames across two interfaces: **13 × `0-2-12` (IR, port 10013)** and **23 × `0-2-10` (depth, port 10010)**. The timestamps are **absolute capture times** (milliseconds since capture start), not inter-frame delays. Because the file is grouped, the timestamp sequence is **not monotonic**: two transitions produce backward stamps (−157 ms at the IR→depth get_frame boundary, −114 ms at the depth→post-stream SetProperty boundary). The replay client clamps negative deltas to 0, so these frames are sent immediately after the previous frame with no delay.

The client replays both interfaces over **one connection to port 10013**, despite the frames originating from two different interfaces. A hardware replay that stalls must record the interface suffix (`0-2-12` or `0-2-10`) of the unanswered frame to identify which interface's request was not acknowledged.
