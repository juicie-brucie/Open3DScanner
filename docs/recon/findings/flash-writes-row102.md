# Flash Writes During Normal Operation — Row #102 / OQ-F3

*Observation-derived from session4 capture (2026-10-18, 86 s window). `[inferred]` — no vendor binary, no gated run. Captures stay outside repo.*

## 1. The Question

Does the vendor's own client issue **any write that persists to the device** — flash memory write, config/store commit, firmware/parameter write — during normal operation (discovery → start → stream → stop), or are all writes volatile (registers, session state)?

This matters for C2 (#100): if there's a required persistent write we missed, claiming the device is false.

## 2. The Inventory — Every Observed Command, Classified

### 2.1 GVCP Commands (UDP 3956)

| Command | Count | Classification |
|---------|-------|----------------|
| DISCOVERY_CMD (`42 11 00 02 00 00 00 01`) | ~43 over 86 s | **Read** — queries device presence |

**Verdict**: GVCP is read-only. No persistent writes.

### 2.2 RPC Methods (TCP, msgpack-RPC)

| Method | Count | Classification | Rationale |
|--------|-------|----------------|-----------|
| `ob_get_device_enumeration_info` | 1 | **Read** | returns device metadata |
| `ob_get_stream_profile_list` | 1 | **Read** | returns profile list |
| `ob_rpc_get_frame` | 14,680 | **Read** | returns frame data |
| `ob_rpc_get_imu_frame` | 15,892 | **Read** | returns IMU samples |
| `ob_rpc_get_device_report_infomation` | 671 | **Read** | returns diagnostic data |
| `ob_rpc_start_stream` | 2 | **Volatile write** | starts stream state (IR + depth) |
| `ob_rpc_stop_stream` | 2 | **Volatile write** | stops stream state |
| `ob_rpc_start_imu_stream` | 1 | **Volatile write** | starts IMU stream |
| `ob_rpc_stop_imu_stream` | 1 | **Volatile write** | stops IMU stream |
| `ob_rpc_get_pu` | 69 | **Read** | reads PU value |
| `ob_rpc_get_pu_range` | 9 | **Read** | reads PU range |
| `ob_rpc_set_pu` | 45 | **Volatile write** | sets PU value (no flash ops follow) |
| `ob_set_ir_frame_light_type` | 4 | **Volatile write** | sets light type (no flash ops follow) |
| `ob_rpc_send_data` | 3,336 | **Wrapper** | carries vendor commands below |

**Verdict**: RPC methods are either reads or volatile state changes. No persistent writes.

### 2.3 Vendor Commands (inside `ob_rpc_send_data`, opcode field)

| Opcode | Name | Count | Classification | Rationale |
|--------|------|-------|----------------|-----------|
| 1 | `GET_PROPERTY` | 267 | **Read** | reads property value |
| 2 | `SET_PROPERTY` | 2,869 | **Volatile write** | writes property (laser/exposure loops, no flash ops follow) |
| 3 | `GET_STRUCTURE_DATA` | 8 | **Read** | reads structure data |
| 17 | `INIT_READ_RAW_DATA` | 2 | **Read** | initializes raw data read |
| 18 | `READ_RAW_DATA` | 4 | **Read** | reads raw data |
| 19 | `FINISH_READ_RAW_DATA` | 2 | **Read** | finalizes raw data read |
| **25** | **flash read (extension)** | **120** | **Read** | reads flash memory (no write opcode observed) |
| 26 | `GET_COMMAND_VERSION_V1_1` | 16 | **Read** | reads version info |
| 27 | `GET_STRUCTURE_DATA_V1_1` | 17 | **Read** | reads structure data |
| **28** | `SET_STRUCTURE_DATA_V1_1` | **1** | **Volatile write** | writes structure data (flash reads follow, but no writes) |
| 29 | `INIT_READ_STRUCT_DATA_LIST_V1_1` | 10 | **Read** | initializes structure list read |
| 30 | `READ_STRUCT_DATA_LIST_V1_1` | 10 | **Read** | reads structure list |
| 31 | `FINISH_READ_STRUCT_DATA_LIST_V1_1` | 10 | **Read** | finalizes structure list read |

**Verdict**: All vendor commands are either reads or volatile writes. **No flash write opcode is observed in the entire capture set.**

## 3. The Write Candidates — Deep Dive

### 3.1 `SET_PROPERTY` (opcode 2) — 2,869 calls

**What it does**: Writes property values (laser on/off, exposure, current) at high frequency (~33 Hz for laser loop, ~1.5 s for exposure ramp).

**Evidence it's volatile**:
- High call frequency (2,869 calls in 86 s) — if this were flash, the device would wear out
- No flash operations (opcode 25) follow within 0.5 s for 77% of calls (77/100 sampled)
- The 23% that do have flash reads following are reads, not writes — likely reading calibration data needed after the property change
- Properties are set repeatedly with changing values (laser loop toggles 0↔1, exposure ramps 500k→1.5M) — inconsistent with persistent config

**Raw frame** (opcode 2, prop 3, value 0):
```
474d 0400 0200 <reqid> 03000000 00000000
  ↑    ↑    ↑      ↑       ↑         ↑
  magic size opcode reqid  prop_id   value
```

### 3.2 `ob_rpc_set_pu` — 45 calls

**What it does**: Sets PU (processing unit?) values on the IR interface (port 10013). Observed values: 29 unique values from 1000 to 6000 (within the `ob_rpc_get_pu_range` of [1000, 15000, 100, 1000]).

**Evidence it's volatile**:
- 45 calls in 86 s with varying values (29 unique values observed)
- No flash operations follow within 0.5 s for any of the 45 calls
- If persistent, the device would need to commit after each change — but we see no commit operation

**Raw frame** (msgpack request):
```
94 00 <msgid> d9 1b 6f625f7270635f7365745f7075 302d322d3132 92 cd 07d2 01
  ↑  ↑   ↑      ↑    ↑                           ↑           ↑   ↑    ↑  ↑
  [0  msgid  "ob_rpc_set_pu"                   "0-2-12"    [2002  1]
```

### 3.3 `SET_STRUCTURE_DATA_V1_1` (opcode 28) — 1 call

**What it does**: Writes structure data (structure id 1043). Called once at t=25.047671, during the pre-stream setup phase.

**Evidence it's volatile**:
- Flash reads (opcode 25) follow at t=25.369–25.382 (22 reads in 12 ms), but these are **reads**, not writes
- If this were a flash write, we'd expect a flash write opcode or a commit command — but we see only reads
- The structure id 1043 is read 17 times via opcode 27 (`GET_STRUCTURE_DATA_V1_1`), suggesting it's configuration data that's read frequently but written rarely (if ever, to flash)

**Raw frame** (opcode 28, structure id 1043):
```
474d 1d00 1c00 9a00 13040000 0000551cc6b785df988e243c...
  ↑    ↑    ↑    ↑      ↑          ↑
  magic size opcode reqid struct_id  payload (23 bytes)
```

**Response**:
```
5242 0100 1c00 9a00 0000 000000000000...
  ↑    ↑    ↑    ↑    ↑      ↑
  magic size opcode reqid error=0  padding
```

The response has `error=0` (success), but this only confirms the volatile write succeeded, not that it persisted to flash.

## 4. The Verdict

**In the 1 captured session (session4, 86 s, full discovery → start → stream → stop cycle), no persistent writes to flash were observed.**

The following commands could theoretically write to flash but were observed to be volatile:
- `SET_PROPERTY` (opcode 2): 2,869 calls, no flash writes follow
- `ob_rpc_set_pu`: 45 calls, no flash writes follow
- `SET_STRUCTURE_DATA_V1_1` (opcode 28): 1 call, flash reads follow but no writes

**No flash write opcode exists in the observed command set.** Opcode 25 is explicitly a flash **read** (extension, not in the public header). There is no corresponding flash write opcode in the captures.

## 5. What Would Falsify This

The verdict would be false if:

1. **A flash write opcode exists but was not used in this session.** The public `Protocol.hpp` header does not list a flash write opcode, but opcode 25 (flash read) is also not in the public header — it's a firmware extension. The write path exists — `AbstractDeviceHandle::updateFlashData` targets `0x211000` (laser-plane record, bricking-capable per `FULL-INDEPENDENCE.md` §233, `calibration-store.md` §1.1) — but the wire opcode is unknown and the capture shows normal operation did not invoke it. A symmetric flash write extension could exist but not have been invoked during this 86 s capture.

2. **One of the "volatile" writes actually commits to flash on a timer or trigger.** For example, `SET_STRUCTURE_DATA_V1_1` might schedule a flash write that happens later. The 22 flash reads at t=25.369–25.382 (0.32 s after the opcode 28 write) could be reading back the committed data. But this is speculative — the reads are reads, not writes.

3. **A different operational mode triggers flash writes.** The capture is a single session (scan → stop). Other modes (firmware update, factory reset, calibration save) might write to flash. But the question is about **normal operation**, and this capture covers the full normal workflow.

## 6. What Capture Would Settle It

To confirm the verdict, the operator should record:

**A multi-session capture showing the same flash region before and after a write operation:**
1. Power on the device, record a full session (discovery → start → stream → stop)
2. Power off the device
3. Power on again, record another full session
4. Compare the flash contents (via opcode 25 reads) between the two sessions
5. If the flash contents are identical, the writes are volatile — verdict confirmed
6. If the flash contents differ, identify which operation caused the change — verdict falsified

**Specifically, check:**
- Structure id 1043 (written once via opcode 28): does it persist across power cycles?
- PU values (set 45 times via `ob_rpc_set_pu`): do they persist across power cycles?
- Property values (set 2,869 times via `SET_PROPERTY`): do they persist across power cycles?

**Alternative: a targeted capture during a "save configuration" UI action (if one exists).** If the UI has a "save settings" button, capture that action. If it triggers a flash write, we'll see a new opcode or a commit command. If it doesn't, the verdict is confirmed.

## 7. Implications for C2 (#100)

**C2 can safely claim the device.** There is no evidence that normal operation requires persistent writes to flash. All observed writes are volatile state changes (stream control, property updates, PU adjustments). The device's flash contains factory calibration data (read via opcode 25) and possibly device configuration (structure id 1043, read via opcode 27), but the captures show no writes to these regions during normal operation.

**Caveat**: C2 should not modify the device's flash without first confirming that the device does not require specific flash content to operate. The captures show the device reads flash at startup (t≈2.88–2.95 s, ~98 reads) and again at project-open (t≈25.37–25.38 s, 22 reads), 120 reads in all, loading calibration data into RAM. The write path exists — `AbstractDeviceHandle::updateFlashData` targets `0x211000` (laser-plane record, bricking-capable per `FULL-INDEPENDENCE.md` §233, `calibration-store.md` §1.1) — but the wire opcode is unknown and the capture shows normal operation did not invoke it. If C2 corrupts the flash, the device may fail to initialize. But this is a separate concern from the current question (are writes required during normal operation — answer: no).

## 8. Summary

| Question | Answer |
|----------|--------|
| Are there persistent writes during normal operation? | **No** |
| What write operations are observed? | Volatile writes only: `SET_PROPERTY` (2,869×), `ob_rpc_set_pu` (45×), `SET_STRUCTURE_DATA_V1_1` (1×) |
| Is there a flash write opcode? | **Not observed** — opcode 25 is flash read only |
| Can C2 claim the device? | **Yes** — no persistent writes required for normal operation |
| What would falsify this? | A multi-session capture showing flash content changes after a write operation |
| What capture is needed to confirm? | Power-cycle test: record flash before/after a session, compare |

---

**Wall**: `docs/recon/**`
**Status**: `[inferred]` — observation-derived from 1 capture session, no vendor binary, no gated run.
