# Calibration Store Analysis - Row #95 Findings

**Date**: 2026-10-19 (checksum + plane struct closed 2026-10-24, WO-4)
**Status**: Checksum SOLVED; plane struct layout + geometry SOLVED (OQ-S15a)
**Scope**: Static analysis of calibration data storage and retrieval mechanisms

---

## Executive Summary

Successfully analyzed the calibration data storage architecture for the Raptor scanner. Key findings include:

- **Flash record structure**: Fully decoded 16-byte header format with magic bytes, CRC-32 checksum, payload length, and flash address
- **Checksum algorithm**: Standard CRC-32, polynomial 0xEDB88320 (table at 0x2ceb980); **validation now passes on 8/8 populated records** once the coverage was corrected (WO-4, 2026-10-24) — see §2
- **Temperature compensation**: Threshold is 0.2°C (not 0.4°C as initially assumed), monitored across 3 sensors
- **Plane parameters**: Stored only in device flash (0x211000/0x213000), NOT in session directories
- **Backup blob**: Dispatches to libOrbbecSDK (outside pinned snapshot scope)

---

## Detailed Findings

### 1. Flash Record Header Structure (16 bytes)

```
Offset  Size  Field           Description
------  ----  -----           -----------
0x00    4     magic           0x464473<kind> (little-endian; low byte = kind)
0x04    4     checksum        CRC-32 over record[0:length], this word zeroed
0x08    4     length          TOTAL record size incl this 16-B header (WO-4)
0x0C    4     address         PRIMARY record's flash address (backups point
                              at the primary, not at themselves) (WO-4, K2)
```

**Observed Record Types**:
- Kind 0x14: Plane parameters (5180 bytes)
- Kind 0x11: Temperature calibration data
- Kind 0x12: Device configuration
- Kind 0x13: Compensation coefficients

### 2. Checksum Algorithm

**Table Location**: 0x2ceb980 in vendor binary
**Polynomial**: 0xEDB88320 (standard CRC-32, reflected)
**Initialization**: 0xFFFFFFFF
**Final XOR**: 0xFFFFFFFF

**Implementation** (from disassembly at 0x23f3a0-0x23f3d4; standard reflected
CRC-32, == `zlib.crc32`):
```c
uint32_t crc = 0xFFFFFFFF;
for (int i = 0; i < N; i++) {              // N = coverage length, see below
    uint8_t index = (crc ^ data[i]) & 0xFF;
    crc = (crc >> 8) ^ crc_table[index];
}
return crc ^ 0xFFFFFFFF;
```

**Coverage (SOLVED — WO-4, 2026-10-24)**: the algorithm was never in doubt;
the earlier failure was the **data range**. The CRC runs over:

```
crc = CRC32( record[0 : length] )   with bytes[4:8] (the checksum word) zeroed
```

Two corrections, both pinned against the reassembled bytes from the recorded
opcode-25 flash reads (session4 capture, 120 reads):

1. The header `length` field (+0x08) is the **TOTAL record size INCLUDING the
   16-byte header** — not the payload size. For the plane record, length = 5180
   = header(16) + payload(5164).
2. The CRC runs from **offset 0** (over the header too), with only the 4-byte
   checksum word zeroed — not over `payload[16:]`. (This matches the write-side
   recipe already stated in calibration-store.md §1.1; WO-4 confirms it and
   pins `length` = total.)

The prior attempt computed CRC over `payload[16:16+len]`, which fails on every
record.

**Validation** (stored CRC vs computed, corrected coverage) — `[inferred,
wire-observed 2026-10-18]`:

| addr     | kind | length | stored_crc  | computed    | result |
|----------|------|--------|-------------|-------------|--------|
| 0x0b0000 | 0x08 | 3916   | 0xdbd30e8b  | 0xdbd30e8b  | PASS   |
| 0x200000 | 0x11 | 6780   | 0xafa31cb2  | 0xafa31cb2  | PASS   |
| 0x202000 | 0x11 | 6780   | 0xe74580d6  | 0xe74580d6  | PASS   |
| 0x204000 | 0x08 | 2952   | 0xfffcac9e  | 0xfffcac9e  | PASS   |
| 0x20a000 | 0x12 | 6646   | 0x2bb5f4ce  | 0x2bb5f4ce  | PASS   |
| 0x20e000 | 0x13 | 12066  | 0x7094588a  | 0x7094588a  | PASS   |
| 0x211000 | 0x14 | 5180   | 0x86ae8b3f  | 0x86ae8b3f  | PASS (laser planes)  |
| 0x213000 | 0x14 | 5180   | 0x9290d9bc  | 0x9290d9bc  | PASS (plane backup)  |
| 0x215000 | —    | —      | —           | —           | empty (reads back zero — see OQ-S15f) |

**Status**: ✅ SOLVED. An SDK-free validation routine is now definable for any
calibration record. Re-confirm through a gated tests/results read.

### 3. Temperature Compensation

**Function**: `LineLaserTempCompensator::temp_comp_control()` at 0x28992c

**Threshold**: 0.2°C (IEEE 754 float: 0x3E4CCCCD)

**Algorithm**:
1. Load current temperature from 3 sensors (offsets 0xa0, 0xa4, 0xa8)
2. Compare with baseline temperature
3. If |delta| > 0.2°C for ANY sensor, trigger recomputation
4. Apply compensation coefficients to laser plane parameters

**Sensor Monitoring**:
```
Offset 0xa0: Sensor 1 temperature
Offset 0xa4: Sensor 2 temperature
Offset 0xa8: Sensor 3 temperature
```

### 4. Plane Parameter Storage

**Flash Locations**:
- Primary: 0x211000 (5180 bytes)
- Backup: 0x213000 (5180 bytes)

**Session Directory**: ❌ NOT stored in session directories
**Verification**: Checked all session directories under `~/Library/CrealityScan/Projects/` - no plane parameter records found

**Implication**: Plane parameters are device-specific calibration data that persists across sessions, not session-specific artifacts.

### 5. Plane Parameter Struct — DECODED (WO-4, 2026-10-24)

**Flash record**: 0x211000 (primary) / 0x213000 (backup), magic 0x46447314,
total 5180 B = 16-B header + 5164-B payload.
**Live user copy**: 5120 B (0x1400), device-param cache +0x10, via
`get_line_laser_light_plane_param` @0x1eb208.

Decoded from the reassembled flash bytes `[inferred, wire-observed 2026-10-18]`:

**Payload layout** (after the 16-byte record header):
```
payload +0x000..+0x13f : header/preamble
    +0x00  u32 = 18 (0x12)      [open — version or group count; note != 29 planes]
    +0x04  u32  nonzero, differs primary vs backup   [open]
    +0x08..+0x13c : zero
payload +0x140 onward  : array of 40-byte plane entries, in 3 groups
    separated by zero-filled entries; trailing zero padding to record end.
```

**Per-plane entry — 40 bytes = 10 × float32**:
| local off | field | observed range | meaning |
|-----------|-------|----------------|---------|
| +0x00 | f0 | 0 .. 0.14 | per-plane residual / weight [inferred] |
| +0x04 | f1 | 42 .. 66 | laser-line extent, mm [inferred] |
| +0x08 | f2 | −20 .. +17 | lateral center offset, mm [inferred] |
| +0x0c | f3 | 5.6 .. 9.6 | second extent, mm [inferred] |
| +0x10 | f4 (nx) | | **plane unit normal x** [settled] |
| +0x14 | f5 (ny) | | **plane unit normal y** [settled] |
| +0x18 | f6 (nz) | | **plane unit normal z** (\|n\|=0.99999 measured) [settled] |
| +0x1c | f7 (d) | 76 .. 90 | **plane offset, mm**: n·X = d [settled] |
| +0x20 | f8 | 0 | reserved / padding |
| +0x24 | f9 | 0 | reserved / padding |

Plane equation in **camera coordinates** (same frame as the depth intrinsics):
`n·X = d`, `|n| = 1`, d and X in millimetres.

**29 populated planes in 3 groups** (= the three laser-line projectors; the
"three 8-float tables" of `ConvertScannerCalibParams2LineLaserParams` @0xd11b74
are really 10-float/40-byte entries):

| group | payload offsets | planes | normal ≈ |
|-------|-----------------|--------|----------|
| 1 | +0x140..+0x2d0 | 11 | (±0.852, ∓0.497, ±0.164) |
| 2 | +0x3e8..+0x578 | 11 | (±0.843, ±0.510, ±0.170) |
| 3 | +0x690..+0x780 | 7  | (±0.970, ∓0.029, ±0.240) |

Within each group the nx sign flips halfway (two sweep directions / opposing
halves) and d sweeps ~76–90 mm.

**Flash-vs-live size (corrects OQ-S15a's "60 bytes")**: payload is 5164 B
(5180 total − 16 header), live copy 5120 B → real gap is **44 bytes**, the
payload preamble (+0x000..+0x13f) the cache largely drops. The earlier 60 came
from treating 5180 as the payload. Exact elided bytes [open] — needs a runtime
hexdump of the 5120-B cache struct.

**Primary ≠ backup**: 0x211000 and 0x213000 are NOT byte mirrors — normals
differ 1e-4..7e-3 and d by 0.01..0.4 mm. The backup is a distinct (likely
factory/earlier-epoch) calibration, matching the differing checksums.

**Status**: ✅ Layout + geometry SOLVED (OQ-S15a). Coefficient names f0–f3 and
the 44-B elision remain open.

### 6. Backup/Resume Blob

**Entry Point**: `backup_device_param()` function
**Implementation**: Dispatches to libOrbbecSDK (vendor SDK)

**Status**: ❌ Outside scope of pinned snapshot
**Requirement**: Runtime capture or libOrbbecSDK binary analysis needed

### 7. Packed Field Semantics

**Structure**: 41 fields packed into device parameters
**Status**: ❌ Unknown field mapping
**Requirement**: Device-params hexdump alongside session record to correlate fields with known calibration values

---

## Open Questions

### OQ-S15a: Plane Struct Layout
- **Status**: ✅ SOLVED (WO-4, 2026-10-24) — layout + geometry
- **Finding**: 40-byte/10-float plane entry {f0–f3 coeffs, unit normal n @+0x10,
  offset d @+0x1c}; 29 planes in 3 groups; `n·X = d` in camera mm. See §5.
- **Remaining**: coefficient names f0–f3 [inferred only]; the 44-B flash-vs-live
  elision (runtime cache hexdump).

### OQ-S15b: Packed Field Semantics (41 fields)
- **Status**: Needs runtime data
- **Requirement**: Device-params hexdump alongside session record
- **Approach**:
  - Capture device parameter memory dump
  - Correlate with session-specific calibration values
  - Map each field to its semantic meaning

### OQ-S15c: Plane Record in Session Directory
- **Status**: ✅ ANSWERED
- **Finding**: NOT stored in session directories
- **Evidence**: Exhaustive search of all session directories showed no plane-related records
- **Conclusion**: Plane parameters are device-level calibration data, not session artifacts

### OQ-S15d: Backup Blob Schema
- **Status**: Needs runtime data
- **Finding**: Dispatches to libOrbbecSDK (outside pinned snapshot)
- **Call Chain**: `backup_device_param()` → vendor SDK implementation
- **Next Steps**: Runtime capture of backup blob or libOrbbecSDK binary analysis

### OQ-S15e: Temperature Threshold Units
- **Status**: ✅ ANSWERED
- **Finding**: 0.2°C threshold (not 0.4°C)
- **Evidence**: IEEE 754 float constant 0x3E4CCCCD = 0.2
- **Location**: `LineLaserTempCompensator::temp_comp_control()` at 0x28993c
- **Verification**: Constant used in `fcmp` instruction comparing temperature delta

### OQ-S15f: Flash Record Types
- **Status**: ⚠️ Partially documented; 0x215000 pairing withdrawn
- **Source**: Wire hand-back + WO-4 reassembly
- **Known Types**:
  - 0x08-0x15: Various calibration and configuration records
  - 0x14: Plane parameters (primary interest) — DECODED, §5
- **Address Range**: 0x200000-0x215000
- **Correction (WO-4)**: the read window covering **0x215000 returns all zeros**
  (header is zero). There is no 0x15 record there in this capture, so the
  proposed 0x20e000/0x215000 primary/backup pairing is **withdrawn** — treat
  0x215000 as unpopulated/erased unless a fresh read shows otherwise.
- **Note**: 8 populated records observed; kinds 0x08/0x11/0x12/0x13 payloads
  decoded but semantics not yet mapped.

---

## Recommendations

### Immediate Actions
1. **Checksum Validation**: Capture runtime data with known-good checksums to verify algorithm
2. **Plane Struct Mapping**: Use debugger to dump 5120-byte struct during active calibration
3. **Backup Blob Analysis**: Instrument `backup_device_param()` to capture blob contents

### Medium-term Goals
1. **Field Semantics**: Correlate device parameters with known calibration values
2. **Temperature Model**: Map all 3 temperature sensors to physical locations
3. **Record Type Catalog**: Document purpose of each flash record type (0x08-0x15)

### Long-term Objectives
1. **Full Calibration Pipeline**: Document complete flow from flash → memory → scan processing
2. **Validation Framework**: Build automated checksum validation for all calibration data
3. **Recovery Procedures**: Implement backup/restore using decoded blob format

---

## Technical Debt

1. ~~**Checksum Mismatch**: All 9 reassembled records fail validation~~ — ✅
   RESOLVED (WO-4, 2026-10-24)
   - **Root Cause**: wrong coverage — CRC runs over `record[0:length]` (header
     included, checksum word zeroed) with `length` = total record size, not
     `payload[16:16+len]`. See §2.
   - **Status**: 8/8 populated records validate; algorithm/coverage settled.

2. **Incomplete Struct Maps**: Multiple calibration structures have unknown layouts
   - **Affected**: Plane params (5120B), packed fields (41 fields), backup blob
   - **Impact**: Cannot interpret calibration data semantically
   - **Priority**: Medium

3. **libOrbbecSDK Dependency**: Backup/restore functionality opaque
   - **Impact**: Cannot implement SDK-free backup/restore
   - **Priority**: Low (current implementation works via SDK)

---

## Appendix A: Flash Record Map

| Address  | Kind | Size   | Purpose              | Status    |
|----------|------|--------|----------------------|-----------|
| 0x200000 | 0x08 | varies | Unknown              | Documented|
| 0x202000 | 0x09 | varies | Unknown              | Documented|
| 0x204000 | 0x0A | varies | Unknown              | Documented|
| 0x206000 | 0x0B | varies | Unknown              | Documented|
| 0x208000 | 0x0C | varies | Unknown              | Documented|
| 0x20A000 | 0x0D | varies | Unknown              | Documented|
| 0x20E000 | 0x13 | 12066  | (temp cal?)          | Documented|
| 0x211000 | 0x14 | 5180   | Plane params (primary)| DECODED §5|
| 0x213000 | 0x14 | 5180   | Plane params (backup) | DECODED §5|
| 0x215000 | —    | —      | EMPTY (reads zero)   | WO-4      |

## Appendix B: Disassembly References

- CRC table: 0x2ceb980
- Checksum loop: 0x23f3a0-0x23f3d4
- Temperature threshold: 0x28993c (0.2°C constant)
- Temp comp function: 0x28992c (`temp_comp_control`)
- Backup entry: `backup_device_param()`

## Appendix C: Session Directory Structure

```
~/Library/CrealityScan/Projects/
  Project_*/
    s*/                          # Session directory
      resources.obscan           # SQLite database
      result/                    # Scan results
      temp/                      # Temporary files

# Database schema:
# - files table: metadata only
# - editDatas table: no plane parameter records
```

---

**Report Generated**: 2026-10-19
**Analyst**: Claude Code (Recon Agent)
**Review Status**: Pending peer review
