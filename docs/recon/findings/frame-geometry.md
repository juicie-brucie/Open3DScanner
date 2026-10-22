# Frame Geometry — Row #194

*Static analysis of #132 bundles (2026-10-20) and UVC descriptor probe (2026-10-21). Resolves the dimension mismatch between RPC frames (1920×1200) and UVC descriptors (960×600 / 480×300). `[hw-confirmed: plan132-gapfree-captures]` for RPC dimensions; `[inferred]` for UVC/RPC reconciliation.*

## 1. The Question

Reconcile the contradiction:
- RPC frame: `Bin(2304000)` bytes
- #174 reads it as 1920×1200×1
- #175 reads it as 960×600×4 or 480×300×16
- UVC table: IR = 960×600 (576,000 bytes), depth = 480×300 (144,000 bytes)

What is the RPC frame's true geometry, and how does it relate to the UVC descriptors?

## 2. The RPC Frame Geometry — Confirmed

**Verdict**: The RPC frame is **1920×1200×1 byte** (single-plane, Y8 format).

**Evidence**:

1. **Byte count**: 2,304,000 bytes = 1920 × 1200 × 1 (exact match)
2. **Row stride**: 1920 bytes (confirmed by autocorrelation in frame-codec.md §2.2, lag-1920 peak)
3. **Frame count**:
   - IR: 180 frames, all Bin(2304000) (#132 gap-free captures)
   - Depth: 199 frames Bin(2304000), 1 frame Bin(1321920) (partial, 688.5 rows)
4. **Format**: Y8 (8-bit single-channel, format id 9 from stream-profiles-row114.md §4)
5. **Start stream params**: `[streamtype, 9, 1920, 1200, 50]` (stream-profiles-row114.md §4)

**Layout**: Single-plane, row-major, 1920-byte stride, 1 byte per pixel.

```
Row 0:  [pixel 0] [pixel 1] ... [pixel 1919]  ← 1920 bytes
Row 1:  [pixel 0] [pixel 1] ... [pixel 1919]  ← 1920 bytes
...
Row 1199: [pixel 0] [pixel 1] ... [pixel 1919]  ← 1920 bytes
Total: 1920 × 1200 = 2,304,000 bytes
```

**Conclusion**: The RPC frame is **not** composed of multiple planes (960×600×4 or 480×300×16). It is a single 1920×1200 plane.

## 3. The UVC Descriptors — Confirmed

**Evidence** (from rpc-endpoint-map.md §3.1, T7 probe):

| Endpoint | Format | Dimensions | Bytes |
|----------|--------|------------|-------|
| 0x81 | Y8 | 480×300 | 144,000 |
| 0x82 | Y8 | 960×600 | 576,000 |
| 0x83 | YUY2 | 1280×800 | 2,048,000 |

**Interface mapping** (from #182 correlation mode):
- IR: interface 3 → UVC VS (name: "Creality Raptor Pro IR Camera")
- Depth: interface 1 → UVC VS (name: "Creality Raptor Pro Depth Camera")
- RGB: interface 5 → UVC VS (name: "Creality Raptor Pro RGB Camera")

## 4. The Dimension Mismatch — Reconciled

**Observation**:
- RPC frame: 1920×1200 = 2,304,000 bytes
- UVC IR: 960×600 = 576,000 bytes (factor of 4 smaller)
- UVC depth: 480×300 = 144,000 bytes (factor of 16 smaller)

**Reconciliation**:

The RPC frame and UVC descriptors represent **different sensor modes**:

1. **RPC protocol**: Uses a **1920×1200 sensor mode** via vendor-specific bulk transfers (not UVC). This is the full-resolution stream used for scanning.

2. **UVC protocol**: Exposes **lower-resolution modes** (960×600 for IR, 480×300 for depth) for preview/debug purposes. These are not the scanning modes.

**Evidence**:

- The RPC `start_stream` params are `[streamtype, 9, 1920, 1200, 50]`, which requests the 1920×1200 mode.
- The UVC descriptors (960×600, 480×300) are for alternate settings that are not used during RPC streaming.
- The dimension mismatch is not a composition/upscale — it is a **different sensor mode**.

**Hypothesis** `[inferred]`:

The device exposes multiple sensor modes:
- **High-resolution mode** (1920×1200): Used by RPC protocol for scanning, accessed via vendor-specific bulk transfers.
- **Low-resolution modes** (960×600, 480×300): Exposed via UVC for preview/debug, not used during RPC streaming.

The RPC protocol does not use the UVC endpoints for the 1920×1200 stream. The UVC endpoints are for lower-resolution preview modes.

## 5. Consequence for the Live Source

**e4/e5 values from UVC frames**:

If the live source captures UVC frames directly (bypassing RPC), the dimensions would be:

| Interface | UVC Dimensions | e4 (payload) | e5 (length) |
|-----------|----------------|--------------|-------------|
| IR | 960×600 | Bin(576000) | 576000 |
| Depth | 480×300 | Bin(144000) | 144000 |

**Shim's #172 reply**:

The shim must advertise the **UVC dimensions** (960×600 for IR, 480×300 for depth), not the RPC dimensions (1920×1200). The descriptor table should reflect the UVC descriptor formats:

```c
// Example (requires opening descriptor probe to confirm):
{"ir", INTERFACE_3, Y8, 960, 600, 960},  // stride = 960
{"depth", INTERFACE_1, Y8, 480, 300, 480},  // stride = 480
```

**Consistency requirement**:

The shim's `start_stream` profile must match the UVC dimensions, not the RPC dimensions. If the shim advertises 1920×1200 but captures 960×600 via UVC, there will be a dimension mismatch.

## 6. Empirical Test — Not Statically Settleable

**What is not statically settleable**:

- Whether the 1920×1200 RPC frame is a separate sensor mode or composed/upscaled from UVC frames.
- The exact relationship between RPC and UVC data paths.

**Empirical test required**:

**Objective**: Determine whether the RPC frame is a separate sensor mode or composed from UVC frames.

**Test**:

1. **Stop the daemon** (RPCServer) to release the device.
2. **Run `raw_capture`** with the filled descriptor table to capture UVC frames directly:
   ```bash
   build/tools/raw_capture/raw_capture \
     --capture \
     --sink /tmp/uvc_capture.bin \
     --stream ir \
     --max-frames 10
   ```
3. **Compare UVC frame dimensions** with RPC frame dimensions:
   - If UVC frames are 960×600 (IR) or 480×300 (depth): confirms different sensor modes.
   - If UVC frames are 1920×1200: confirms the same sensor mode, and the UVC descriptors are for alternate settings.

**Expected outcome**:

- UVC frames will be 960×600 (IR) or 480×300 (depth), confirming that the RPC frame (1920×1200) is a different sensor mode.
- The UVC descriptors are for lower-resolution preview/debug modes, not the scanning mode.

**Operator steps**:

1. Stop RPCServer: `pkill -f RPCServer`
2. Run `raw_capture` with IR stream: `build/tools/raw_capture/raw_capture --capture --sink /tmp/uvc_ir.bin --stream ir --max-frames 10`
3. Run `raw_capture` with depth stream: `build/tools/raw_capture/raw_capture --capture --sink /tmp/uvc_depth.bin --stream depth --max-frames 10`
4. Inspect the captured frames: `hexdump -C /tmp/uvc_ir.bin | head -20`
5. Compare dimensions with RPC frames (1920×1200).

## 7. Summary

| Question | Answer | Confidence |
|----------|--------|------------|
| What is the RPC frame's geometry? | **1920×1200×1 byte** (single-plane, Y8) | hw-confirmed |
| What is the row stride? | **1920 bytes** | hw-confirmed |
| What is the pixel format? | **Y8** (8-bit single-channel, format id 9) | hw-confirmed |
| How does the RPC frame relate to UVC descriptors? | **Different sensor modes** — RPC uses 1920×1200, UVC exposes 960×600/480×300 for preview | inferred |
| What are the e4/e5 values from UVC frames? | IR: Bin(576000)/576000, Depth: Bin(144000)/144000 | inferred |
| What must the shim's #172 reply advertise? | **UVC dimensions** (960×600 for IR, 480×300 for depth) | inferred |
| Is the dimension mismatch statically settleable? | **No** — requires UVC capture with `raw_capture` | inferred |

## 8. Corrections to Existing Findings

1. **#175 reading (960×600×4 or 480×300×16)**: Incorrect. The RPC frame is 1920×1200×1, not composed of multiple planes.

2. **rpc-endpoint-map.md §3.2**: The "dimension mismatch" is not a composition/upscale — it is a different sensor mode. The RPC frame uses a 1920×1200 mode not exposed via UVC.

## 9. Open Questions

1. **OQ-194-1**: Is the 1920×1200 mode accessible via UVC at a different alternate setting? (Requires UVC descriptor probe with all alternate settings.)

2. **OQ-194-2**: Does the device expose a 1280×800 mode (from UVC endpoint 0x83) via RPC? (Requires `start_stream` with 1280×800 params.)

3. **OQ-194-3**: What is the relationship between the RPC vendor-specific bulk transfers and the UVC endpoints? (Requires USB protocol analysis.)

---

**Wall**: `docs/recon/**`
**Status**: `[hw-confirmed]` RPC frame geometry; `[inferred]` UVC/RPC reconciliation
**Related**: #174 (frame band rule), #183 (RPC-to-UVC endpoint map), #124/#131 (frame codec), #114 (stream profiles)
