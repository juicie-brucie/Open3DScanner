# RPC-to-UVC Endpoint Map — Row #183 / OQ-169e

*Static analysis attempt from vendor binary and existing findings. No device, no new capture. Binary analysis tools were unavailable; this finding documents what is known and specifies the empirical tests needed.*

## 1. The Question

Map the two RPC interfaces (IR 10013, depth 10010) to the UVC VS interfaces/endpoints (0x81, 0x82, 0x83), reconcile the frame dimensions, and map the OB words (e1, e2, e3, e10).

## 2. What Is Known from Existing Findings

### 2.1 RPC Interfaces (from stream-profiles-row114.md)

| Interface | Port | Suffix | Stream Type | Profiles |
|-----------|------|--------|-------------|----------|
| RGB | 10012 | 0-2-14 | 2 (inferred) | 23 |
| IR | 10013 | 0-2-12 | 6 | 54 |
| Depth | 10010 | 0-2-10 | 7 | 89 |

**Stream profiles actually used** (from session4 capture):
- IR: `[6, 9, 1920, 1200, 50]` — stream type 6, format 9, 1920×1200@50
- Depth: `[7, 9, 1920, 1200, 50]` — stream type 7, format 9, 1920×1200@50

**Available resolutions** (from profile lists):
- IR: 960×600, 1280×800, 1520×1200, 1600×1000, 1920×1200
- Depth: 480×300, 960×600, 1280×800, 1520×1200, 1600×1000, 1920×1200

### 2.2 UVC VS Endpoints (from T7 probe, plan178-probe.md)

| Endpoint | Format | Resolution |
|----------|--------|------------|
| 0x81 | Y8 | 480×300 |
| 0x82 | Y8 | 960×600 |
| 0x83 | YUY2 | 1280×800 |

**HID interface**: Interface 6, endpoint 0x85, interrupt, max packet 248 bytes, report descriptor 25 bytes.

### 2.3 RPC Frame Structure (from frame-codec.md, #174)

RPC frames are `Bin(2304000)` = 1920×1200×1 byte = **whole frames**, not bands.

The 11-element array structure:
- e0: u64 (always 0)
- e1: u64 (timestamp, µs)
- e2: u64 (second time/counter word)
- e3: u32 (format, always 255 on wire)
- e4: bin (frame payload, 2304000 bytes)
- e5: u32 (payload length)
- e6: bin (secondary buffer, 0 bytes)
- e7: u32 (secondary length)
- e8: bin (tertiary buffer, 6 bytes)
- e9: u32 (tertiary length)
- e10: u32 (IR light type: 0 for depth, 2 for IR)

## 3. The Dimension Mismatch

**Critical observation**: The RPC frames are 1920×1200, but the UVC VS descriptors are 480×300, 960×600, and 1280×800. These dimensions do not match.

### 3.1 Possible Explanations

1. **Composition/fusion**: The 1920×1200 RPC frame is composed from multiple UVC streams (e.g., stereo IR + depth fusion).
2. **Different path**: The RPC protocol uses a different data path than UVC (e.g., vendor-specific bulk transfer, not UVC).
3. **Scaling/composition step**: There is an intermediate processing step that scales or composes the UVC frames into the RPC frame.
4. **Mode-dependent**: The UVC descriptors represent different modes, not simultaneous streams. The 1920×1200 mode is not exposed via UVC.

### 3.2 What the Evidence Suggests

The RPC stream profiles include 1920×1200, but the UVC descriptors only go up to 1280×800. This suggests that:
- The 1920×1200 mode is **not exposed via UVC**
- The RPC protocol uses a **different data path** than UVC
- The UVC endpoints (0x81, 0x82, 0x83) represent **lower-resolution modes** (480×300, 960×600, 1280×800)

**Hypothesis**: The RPC protocol does not use the UVC endpoints for the 1920×1200 stream. The UVC endpoints are for lower-resolution preview/debug modes, while the RPC protocol uses a vendor-specific bulk transfer for the full-resolution stream.

**Confidence**: `[inferred]` — based on dimension mismatch, not confirmed by binary analysis.

## 4. RPC-to-UVC Mapping (Static Analysis Failed)

**Status**: Could not be determined statically. The binary analysis tools were unavailable, and the vendor binary does not contain obvious symbols mapping RPC ports to UVC endpoints.

### 4.1 What We Can Infer

Based on the available resolutions:
- UVC 0x81 (480×300): Only depth offers this resolution → **likely depth**
- UVC 0x82 (960×600): Both IR and depth offer this → **ambiguous**
- UVC 0x83 (1280×800): Both IR and depth offer this → **ambiguous**

But this is speculative. The actual mapping requires empirical testing.

### 4.2 Empirical Test Required

**Test**: Run the vendor daemon streaming IR and depth simultaneously, and observe which UVC VS endpoints are active.

**Steps**:
1. Start the vendor daemon (CrealityScan or RPCServer)
2. Start streaming IR on port 10013
3. Start streaming depth on port 10010
4. Use `system_profiler SPUSBDataType` or a USB protocol analyzer to observe which UVC endpoints are active
5. Compare the frame dimensions and formats to determine the mapping

**Expected result**: Each active RPC stream should correspond to an active UVC endpoint with matching dimensions.

## 5. OB Words Mapping

### 5.1 What the OB Words Are (from frame-codec.md, #131)

The OB words are fields in the 11-element RPC frame array:
- **e1**: Timestamp (µs) — the frame's primary timestamp
- **e2**: Second time/counter word — a second 64-bit time word, unit unknown
- **e3**: Format field — always 255 on wire, not the stream profile's format id
- **e10**: IR light type — 0 for depth, 2 for IR

### 5.2 Mapping to UVC

If the RPC protocol uses UVC, the OB words would map to UVC metadata:
- **e1 (timestamp)**: Could map to UVC PTS (Presentation Time Stamp)
- **e2 (counter)**: Could map to UVC frame counter or clock
- **e3 (format)**: Could map to UVC format index
- **e10 (light type)**: Could map to UVC payload-specific metadata (e.g., IR LED state)

But this is speculative. The actual mapping requires:
1. Confirming that RPC uses UVC (which the dimension mismatch suggests it does not)
2. Empirical testing to correlate OB words with UVC metadata

### 5.3 Empirical Test Required

**Test**: Capture both RPC frames and UVC frames simultaneously, and correlate the OB words with UVC metadata.

**Steps**:
1. Start the vendor daemon
2. Start streaming on an RPC port
3. Simultaneously capture the RPC frames (via msgpack-RPC) and UVC frames (via USB capture)
4. Compare timestamps, frame counters, and metadata fields
5. Determine the mapping

**Expected result**: If RPC uses UVC, the OB words should correlate with UVC metadata fields.

## 6. descriptor_table.h Entries

**Status**: Cannot be determined without empirical testing.

### 6.1 What Would Be Needed

If the RPC protocol uses UVC, the descriptor_table.h would need entries for:
- UVC VS endpoints (0x81, 0x82, 0x83)
- Frame formats and dimensions
- OB word mappings to UVC metadata

But given the dimension mismatch, it's likely that:
- The RPC protocol does **not** use UVC for the 1920×1200 stream
- The descriptor_table.h would only need entries for lower-resolution UVC modes
- The 1920×1200 stream uses a vendor-specific bulk transfer

### 6.2 Empirical Test Required

**Test**: After determining the RPC-to-UVC mapping (section 4.2), create descriptor_table.h entries for the confirmed mappings.

**Steps**:
1. Perform the empirical test from section 4.2
2. For each confirmed RPC-to-UVC mapping, create a descriptor_table.h entry with:
   - UVC endpoint address
   - Frame format (Y8, YUY2, etc.)
   - Frame dimensions
   - OB word mappings (if applicable)
3. For the 1920×1200 stream, create a vendor-specific bulk transfer entry (if confirmed)

## 7. Summary

| Question | Answer | Confidence |
|----------|--------|------------|
| Which UVC endpoint is IR? | **Unknown** — requires empirical testing | — |
| Which UVC endpoint is depth? | **Unknown** — requires empirical testing | — |
| What is endpoint 0x83? | **Unknown** — requires empirical testing | — |
| How do RPC frames (1920×1200) relate to UVC descriptors (480×300, 960×600, 1280×800)? | **Dimension mismatch** — RPC likely uses a different path or composition step | inferred |
| What do the OB words map to in UVC? | **Unknown** — requires empirical testing | — |
| What descriptor_table.h entries are needed? | **Unknown** — requires empirical testing | — |

**Key finding**: The RPC frames are 1920×1200, but the UVC descriptors only go up to 1280×800. This strongly suggests that the RPC protocol does not use UVC for the full-resolution stream. The UVC endpoints likely represent lower-resolution preview/debug modes, while the RPC protocol uses a vendor-specific bulk transfer for the full-resolution stream.

**Next steps**: Empirical testing to confirm the RPC-to-UVC mapping and determine the actual data path.

## 8. Open Questions

1. **Does the RPC protocol use UVC at all?** The dimension mismatch suggests it does not for the 1920×1200 stream, but it might use UVC for lower-resolution modes.

2. **What is the actual data path for the 1920×1200 RPC stream?** If not UVC, is it a vendor-specific bulk transfer, isochronous transfer, or something else?

3. **Are the UVC endpoints (0x81, 0x82, 0x83) used by the RPC protocol, or are they for a different purpose (e.g., preview, debug)?**

4. **What is the relationship between the RPC stream profiles and the UVC descriptors?** Do they represent different modes, or is there a mapping that we're missing?

5. **Do the OB words have any relationship to UVC metadata, or are they RPC-specific?**

---

**Wall**: `docs/recon/**`
**Status**: `[inferred]` — static analysis failed due to tool unavailability; empirical testing required
**Related**: #114 (stream profiles), #124/#174 (frame codec), #131 (frame producer), #169 (USB descriptor map)
**Operator action required**: Empirical test to determine RPC-to-UVC mapping (see sections 4.2, 5.3, 6.2)
