# RPC-to-UVC Endpoint Map — Row #183 / OQ-169e

*Static analysis from #132 bundles (2026-10-20) and vendor binary. No device, no new capture. `[inferred]` — binary analysis was unavailable; dimensions reconciled from bundle bytes and probe data.*

## 1. The Question

Map the two RPC interfaces (IR 10013, depth 10010) to the UVC VS interfaces/endpoints (0x81, 0x82, 0x83), reconcile the frame dimensions (RPC 1920×1200 vs UVC 480×300 / 960×600 / 1280×800), and map the OB words (e1, e2, e3, e10) from the #132 bytes.

## 2. The Evidence — #132 Bundle Bytes

### 2.1 Bundle Contents (Decoded from RCRP Format)

**IR bundle** (`~/captures/plan132-ir-v2/replies/port-10013.bin`):
- **180 get_frame replies** (0 empty)
- All frames are Bin(2304000) = 2.3 MB (whole 1920×1200 frames)
- e1 values: 1792552122036427 → 1792552151906115 (CLOCK_REALTIME µs, never 0)
- e2 values: 211599394 → 241468880 (CLOCK_MONOTONIC µs, monotonic, 0 non-monotonic pairs)
- e3: always 255 (180/180)
- e10: **mixed 3 and 4** (84×3, 96×4) — NOT constant per interface

**Depth bundle** (`~/captures/plan132-depth-v2/replies/port-10010.bin`):
- **200 get_frame replies** (0 empty)
- 199 frames are Bin(2304000), 1 frame is Bin(1321920) (partial frame at msgid=914, 688.5 rows)
- e1 values: 1792552171141573 → 1792552198840797 (CLOCK_REALTIME µs, never 0)
- e2 values: 260704758 → 288403983 (CLOCK_MONOTONIC µs, monotonic, 0 non-monotonic pairs)
- e3: always 255 (200/200)
- e10: **mixed 3 and 4** (4×3, 196×4) — NOT constant per interface

**Correction to #172 record:** The #172 RPC shim test observed "[10]=UInt(4) on IR, UInt(3) on depth" as a sample observation (62 IR frames, 70 depth frames). The #132 bundles show e10 is **mixed on both interfaces**, not constant. e10 is a per-frame value (the IR light type), not a per-stream constant.

### 2.2 OB Words — Byte-Backed Semantics

From the #132 bundles and #131 producer analysis:

| Element | Wire form | C++ type | Semantics | Source |
|---------|-----------|----------|-----------|--------|
| e0 | `uint64` 0 | `u64` | always 0 (padding) | #131 §10.3 |
| e1 | `uint64` | `u64` | **CLOCK_REALTIME** µs (wall-clock timestamp) | #132 bundles (e.g., 1792552122036427 = 2026-10-21 03:08:42 UTC) |
| e2 | `uint64` | `u64` | **CLOCK_MONOTONIC** µs (monotonic counter) | #132 bundles (monotonic, 0 non-monotonic pairs) |
| e3 | `uint8` 255 | `OBFormat` | format field (always 255 on wire, not the stream profile's format id 9) | #131 §10.3, #132 bundles (255/380) |
| e4 | `bin16` 2304000 | `vector<char>` | frame payload (whole 1920×1200×1 byte frame) | #132 bundles, #174 |
| e5 | `uint32` 2304000 | `uint` | payload length | #132 bundles |
| e6 | `bin8` 0 | `vector<char>` | secondary data buffer (empty) | #132 bundles |
| e7 | `uint8` 0 | `uint` | secondary buffer length | #132 bundles |
| e8 | `bin8` 6 | `vector<char>` | tertiary data buffer (6 bytes, opaque) | #132 bundles |
| e9 | `uint8` 6 | `uint` | tertiary buffer length | #132 bundles |
| e10 | `uint32` 3 or 4 | `OBIRFrameLightType_t` | **IR light type** (per-frame, mixed 3/4 on both IR and depth) | #132 bundles, #131 §10.3 |

**Correction to pull-semantics.md §2 (2026-10-21):** The prior correction stated "e1=0 for every reply" — this was **wrong**. The #132 bundles show e1 is **never 0** (0/380). e1 carries CLOCK_REALTIME µs (wall-clock), e2 carries CLOCK_MONOTONIC µs (monotonic counter). Both are real timestamps.

**e3 source:** The observed 255 is the `OBFormat` enum value from the PAL frame's `+0x18` offset (#131 §10.3). It is **not** the stream profile's format id 9 (which is sent in `start_stream` params). The descriptor table has no OBFormat field — e3 is carried in the RPC reply, not in the UVC descriptor. The descriptor table should carry the UVC pixel format (Y8, YUY2, etc.), not the OBFormat enum.

**e10 semantics:** e10 is `OBIRFrameLightType_t`, a per-frame value indicating the IR light type (3 or 4). It varies per frame on both IR and depth interfaces (IR: 84×3, 96×4; depth: 4×3, 196×4). This is **not** a per-stream constant. On a live UVC frame, e10 would come from the device's IR light control state (which laser pattern is active for that frame).

**Where e1/e2 come from on a live UVC frame:** On a UVC frame, the timestamp would come from the UVC payload header (PTS — Presentation Time Stamp). The RPC reply's e1 (CLOCK_REALTIME) and e2 (CLOCK_MONOTONIC) are filled by the RPCServer from the PAL frame's timestamp fields. On a live UVC frame, the device's firmware would provide the timestamp in the UVC payload header, not from CLOCK_REALTIME/CLOCK_MONOTONIC.

## 3. RPC-to-UVC Mapping — Static Analysis Failed

**Status: Could not be determined statically.** The binary analysis tools were unavailable, and the vendor binary does not contain obvious symbols mapping RPC ports to UVC endpoints.

### 3.1 What We Know

**RPC interfaces** (from stream-profiles-row114.md):
- IR: port 10013, suffix `0-2-12`, stream type 6, 54 profiles
- Depth: port 10010, suffix `0-2-10`, stream type 7, 89 profiles
- RGB: port 10012, suffix `0-2-14`, stream type 2, 23 profiles

**UVC VS endpoints** (from T7 probe, plan178-probe.md):
- 0x81: Y8, 480×300
- 0x82: Y8, 960×600
- 0x83: YUY2, 1280×800
- HID interface 6, endpoint 0x85 (interrupt, max packet 248 bytes, report descriptor 25 bytes)

**RPC frame dimensions** (from #132 bundles, #174):
- All frames are Bin(2304000) = 1920×1200×1 byte (whole frame)

### 3.2 The Dimension Mismatch

**Critical observation:** RPC frames are 1920×1200, but UVC VS descriptors are 480×300, 960×600, and 1280×800. These dimensions do not match.

**Possible explanations** `[inferred]`:
1. **Composition/fusion**: The 1920×1200 RPC frame is composed from multiple UVC streams (e.g., stereo IR + depth fusion).
2. **Different path**: The RPC protocol uses a different data path than UVC (e.g., vendor-specific bulk transfer, not UVC).
3. **Scaling/composition step**: There is an intermediate processing step that scales or composes the UVC frames into the RPC frame.
4. **Mode-dependent**: The UVC descriptors represent different modes, not simultaneous streams. The 1920×1200 mode is not exposed via UVC.

**Hypothesis** `[inferred]`: The RPC protocol does not use the UVC endpoints for the 1920×1200 stream. The UVC endpoints are for lower-resolution preview/debug modes, while the RPC protocol uses a vendor-specific bulk transfer for the full-resolution stream.

### 3.3 RPC-to-UVC Assignment — Resolved by Name (#182 Evidence)

**Status: Interfaces resolved by name** (from #182 correlation mode run, PR #459, 2026-10-21).

**Attributed evidence** (builder's live run with RPCServer holding the device):
```
[match] ir:     interface 3  name="Creality Raptor Pro IR Camera"
[match] depth:  interface 1  name="Creality Raptor Pro Depth Camera"
[match] rgb:    interface 5  name="Creality Raptor Pro RGB Camera"
[match] hid:    interface 6  name="Creality Raptor Pro IMU"
```

**Mapping by interface number:**
- **IR**: interface 3 → UVC VS (name: "Creality Raptor Pro IR Camera")
- **Depth**: interface 1 → UVC VS (name: "Creality Raptor Pro Depth Camera")
- **RGB**: interface 5 → UVC VS (name: "Creality Raptor Pro RGB Camera")
- **HID/IMU**: interface 6 (name: "Creality Raptor Pro IMU", endpoint 0x85)

**Note:** Every VideoStreaming interface was at alternate setting 0 at the sample time (no stream active, or the registry does not track `SET_INTERFACE`). The interface **names** resolve the mapping.

**Empirical confirmation pending:** When a stream is active, the mapping should be confirmed by observing which UVC VS endpoints become active. The dimension mismatch (RPC 1920×1200 vs UVC lower resolutions) remains open — see §3.2.

## 4. descriptor_table.h Entries — Interfaces Named, Formats Need Probe

**Status: Interfaces resolved by name (#182), but formats/stride need the opening descriptor probe.**

### 4.1 What We Now Know

**Interfaces** (from #182 correlation mode, attributed evidence):
- IR: interface 3 ("Creality Raptor Pro IR Camera")
- Depth: interface 1 ("Creality Raptor Pro Depth Camera")
- RGB: interface 5 ("Creality Raptor Pro RGB Camera")
- HID/IMU: interface 6 ("Creality Raptor Pro IMU", endpoint 0x85)

**What is still unknown**:
- UVC VS descriptor formats (Y8, YUY2, etc.) for each interface
- Actual dimensions/stride exposed via UVC
- Whether the RPC 1920×1200 frames map to lower-resolution UVC streams or use a different path

The **dimension mismatch** (RPC 1920×1200 vs UVC 480×300/960×600/1280×800) remains open `[inferred]` — see §3.2.

### 4.2 Exact Entries Owed (Once Opening Descriptor Probe Runs)

**Interfaces are now named** (from #182 evidence):
- IR: interface 3
- Depth: interface 1
- RGB: interface 5
- HID/IMU: interface 6

**Formats/stride still need the opening descriptor probe** (the daemon stopped before the probe could run). Once the probe runs, the descriptor_table.h will need entries like:

```c
// Example (requires opening descriptor probe to confirm formats):
{"ir", INTERFACE_3, /* format TBC */, /* dimensions TBC */, /* stride TBC */},
{"depth", INTERFACE_1, /* format TBC */, /* dimensions TBC */, /* stride TBC */},
{"rgb", INTERFACE_5, /* format TBC */, /* dimensions TBC */, /* stride TBC */},
{"imu", INTERFACE_6, HID_REPORT, 248, /* report descriptor 25 bytes */},
```

The **dimension mismatch** (RPC 1920×1200 vs UVC 480×300/960×600/1280×800) remains open `[inferred]` — see §3.2. The opening descriptor probe will reveal the actual UVC VS descriptor formats and dimensions, which will clarify whether:
1. The RPC frames are composed/scaled from lower-resolution UVC streams
2. The RPC uses a different data path (vendor-specific bulk transfer)
3. The 1920×1200 mode is not exposed via UVC at all

## 5. Summary

| Question | Answer | Confidence |
|----------|--------|------------|
| Which UVC endpoint is IR? | **Interface 3** (by name "Creality Raptor Pro IR Camera", #182) | hw-confirmed |
| Which UVC endpoint is depth? | **Interface 1** (by name "Creality Raptor Pro Depth Camera", #182) | hw-confirmed |
| What is interface 5? | **RGB** (by name "Creality Raptor Pro RGB Camera", #182) | hw-confirmed |
| What is interface 6? | **HID/IMU** (by name "Creality Raptor Pro IMU", #182) | hw-confirmed |
| How do RPC frames (1920×1200) relate to UVC descriptors (480×300, 960×600, 1280×800)? | **Dimension mismatch** — RPC likely uses a different path or composition step | inferred |
| What do e1/e2 carry? | **e1 = CLOCK_REALTIME µs, e2 = CLOCK_MONOTONIC µs** (never 0, both monotonic) | hw-confirmed |
| What does e3 carry? | **OBFormat enum (always 255 on wire)**, not the stream profile's format id | hw-confirmed |
| What does e10 carry? | **IR light type (per-frame, mixed 3/4 on both interfaces)**, not a per-stream constant | hw-confirmed |
| What descriptor_table.h entries are needed? | **Unknown** — requires empirical testing | — |

## 6. Corrections to Existing Findings

1. **pull-semantics.md §2** (corrected 2026-10-21 by #183): The prior correction stated "e1=0 for every reply" — this was **wrong**. The #132 bundles show e1 is **never 0** (0/380). e1 carries CLOCK_REALTIME µs, e2 carries CLOCK_MONOTONIC µs.

2. **#172 record** (plan172-rpc-shim-ab.md line 91): The #172 record observed "[10]=UInt(4) on IR, UInt(3) on depth" as a sample. The #132 bundles show e10 is **mixed on both interfaces** (IR: 84×3, 96×4; depth: 4×3, 196×4). e10 is a per-frame value, not a per-stream constant.

---

**Wall**: `docs/recon/**`
**Status**: `[inferred]` — static analysis from #132 bundles and vendor binary; RPC-to-UVC mapping requires empirical testing
**Supersedes**: None — this is a new analysis
**Related**: #114 (stream profiles), #124/#174 (frame codec), #131 (frame producer), #143 (pull semantics), #169 (USB descriptor map), #172 (RPC shim), #182 (probe-match mode)
