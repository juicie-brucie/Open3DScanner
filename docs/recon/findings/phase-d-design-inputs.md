# Phase-D Design Inputs — Rows #201/#202

*Static analysis from #132 bundles, #178 probe, #183 endpoint map, and #194 frame geometry. No new device run. Settles two design inputs for Phase-D (C2: replace the daemon) and flags one hypothesis for empirical test.*

## §1. The HID Interface Is Input-Only (Settled — Row #201)

**Verdict**: The HID interface (interface 6, endpoint 0x85) **cannot carry laser/exposure control**. It is input-only.

**Evidence** (from `tests/results/2026-10-21-plan178-probe.raw.log`):

```
[probe] interface number=6 ... class=0x03 subclass=0x00 protocol=0x00 kind=hid
[probe] endpoint interface=6 ... address=0x85 transfer=interrupt max_packet_size=248 ...
[probe] control.report_descriptor_length=25
```

The 25-byte HID report descriptor (from PR #490, operator's `--probe` run):
```
05 8C 09 01 A1 01 09 03 17 00 00 00 00 27 FF FF FF FF 75 20 95 3E 81 02 C0
```

Decoded:
- `05 8C` — Usage Page (0x008C, vendor-defined)
- `09 01` — Usage (0x0001)
- `A1 01` — Collection (Application)
- `09 03` — Usage (0x0003)
- `17 00 00 00 00` — Logical Minimum (0)
- `27 FF FF FF FF` — Logical Maximum (0xFFFFFFFF)
- `75 20` — Report Size (32 bits)
- `95 3E` — Report Count (62)
- `81 02` — **Input** (Data, Variable, Absolute) ← input field, not output
- `C0` — End Collection

**Total**: 62 × 32 bits = 1984 bits = 248 bytes (matches `max_packet_size=248`).

**Critical observation**: All fields are `81 02` (Input). There are **no Output fields** (`91 xx`) and **no Feature fields** (`B1 xx`). The HID interface is strictly input-only.

**Consequence for Phase-D**: The laser/exposure control **does not use HID output reports**. The control path must be either:
1. The RPC property channel (`ob_rpc_set_property` / `ob_rpc_get_property`, row #101), or
2. UVC Extension Unit controls (the probe enumerates `extension_units=4/8/12` on the three VideoControl interfaces).

**Correction to row #189**: The original premise that HID carries laser/exposure control is **refuted** by this evidence. The control stays on the vendor RPC property channel or UVC extension units.

**Confidence**: `[hw-confirmed: plan178-probe]` — the descriptor is from a live probe, not inferred.

---

## §2. The IR 4× Light-State Aggregation Hypothesis (Flagged — Row #202)

**Hypothesis**: The vendor daemon aggregates **four light-state captures** (`ob_set_ir_frame_light_type` 0..3) into one RPC delivery (2,304,000 bytes), while a raw UVC read gets one light state per frame (576,000 bytes).

**The 4× factor is exact**:
- RPC IR frame: 2,304,000 bytes = 1920×1200×1 (from #194 frame geometry)
- UVC IR frame: 576,000 bytes = 960×600×1 (from #178 probe)
- Ratio: 2,304,000 / 576,000 = **4.0 exactly**

**Evidence cited**:
- `ob_set_ir_frame_light_type` is called in pre-stream setup (`protocol-spec.md` §285, `streaming-replay-payloads.md` §3)
- The property map references light types (`property-semantics-row101.md`)
- The RPC frame is exactly 4× the UVC frame size

**Static refutation**:

The hypothesis is **refuted by static analysis** of the frame serializer (from `frame-codec.md` §10.5):

> "The serializer does **not** split the frame — it is a pass-through. The band rule lives in the serializer."

The serializer (`RPCServer::frameCallback`) copies fields from the PAL frame directly into the RPC reply array. There is **no aggregation logic** in the serializer. The PAL frame is delivered as-is, without composition or aggregation of multiple light states.

Further, `frame-geometry.md` §4 concludes:

> "The RPC frame and UVC descriptors represent **different sensor modes**... The dimension mismatch is not a composition/upscale — it is a **different sensor mode**."

The 1920×1200 RPC frame is a **single sensor mode** at full resolution, not a composition of four 960×600 light states. The 4× factor is due to the resolution difference (2× in width, 2× in height), not aggregation.

**What is evidenced**:
- ✅ The RPC frame is 1920×1200×1 Y8 (single-plane, single sensor mode)
- ✅ The UVC IR is 960×600×1 Y8 (single-plane, different sensor mode)
- ✅ The serializer is a pass-through (no aggregation)
- ✅ `ob_set_ir_frame_light_type` is called, but it sets the light type for the stream, not per-frame aggregation
- ✅ e10 (IR light type) is a per-frame value (mixed 3/4), not a per-stream constant

**What is inferred**:
- ❌ The RPC frame aggregates 4 light states — **refuted by static analysis**

**Empirical test (not required, but definitive if needed)**:

If the static refutation is questioned, the empirical test would be:

1. **Precondition A (UVC frame, vendor stopped)**:
   - Stop RPCServer: `pkill -f RPCServer`
   - Capture UVC IR frame: `build/tools/raw_capture/raw_capture --capture --sink /tmp/uvc_ir.bin --stream ir --max-frames 1`
   - Expected: 576,000 bytes (960×600×1 Y8), single light state

2. **Precondition B (RPC frame, vendor running)**:
   - Start RPCServer: ensure daemon is running
   - Capture RPC IR frame: `build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 8 --frames 1 --dump-replies`
   - Expected: 2,304,000 bytes (1920×1200×1 Y8), single sensor mode

3. **Compare**:
   - If RPC frame is 4× aggregated: the 2.3 MB frame would contain 4 distinct 960×600 regions with different light patterns
   - If RPC frame is a different sensor mode: the 2.3 MB frame is a single 1920×1200 image at full resolution
   - **Expected result**: The RPC frame is a single 1920×1200 image (different sensor mode), **not** 4 aggregated light states

**Confidence**: `[inferred]` for the refutation (based on static analysis of the serializer); `[hw-confirmed: plan132-gapfree-captures]` for the frame dimensions.

---

## §3. The Control Path (Summary)

**Settled candidates**:
1. **RPC property channel** (row #101): `ob_rpc_set_property` / `ob_rpc_get_property` on the depth interface (port 10010). This is the primary control path used by the vendor client during streaming.
2. **UVC Extension Unit controls**: The probe enumerates `extension_units=4` (interface 0), `8` (interface 2), `12` (interface 4) on the three VideoControl interfaces. These may carry vendor-specific controls.

**Ruled out**:
- **HID output reports**: Refuted by §1 (HID is input-only).

**Open question**:
- Which UVC Extension Unit controls map to which properties? This requires REA of the vendor binary or empirical testing with UVC control requests.

**Phase-D implication**: The control path is the RPC property channel (primary) or UVC extension units (secondary). HID is not a candidate.

---

## Summary Table

| Question | Answer | Confidence |
|----------|--------|------------|
| Can HID carry laser/exposure control? | **No** — HID is input-only (no output/feature fields) | hw-confirmed |
| Does the RPC frame aggregate 4 light states? | **No** — it's a different sensor mode (1920×1200 vs 960×600), not aggregation | inferred (static refutation) |
| What is the control path? | RPC property channel (primary) or UVC extension units (secondary) | inferred |

## Open Questions

1. **OQ-201-1**: Which UVC Extension Unit controls map to which properties? (Requires REA or empirical testing.)
2. **OQ-202-1**: If the static refutation is questioned, run the empirical test in §2 to confirm the RPC frame is a single sensor mode, not 4 aggregated light states. (Not required, but definitive.)

## References

- `tests/results/2026-10-21-plan178-probe.raw.log` — HID descriptor (PR #490)
- `docs/recon/findings/rpc-endpoint-map.md` — interface mapping, e10 semantics
- `docs/recon/findings/frame-codec.md` §10.5 — serializer is pass-through
- `docs/recon/findings/frame-geometry.md` — RPC vs UVC dimensions, different sensor modes
- `docs/recon/findings/property-semantics-row101.md` — RPC property channel
- `docs/recon/findings/protocol-spec.md` — `ob_set_ir_frame_light_type` usage
- `docs/recon/findings/streaming-replay-payloads.md` §3 — light type calls

---

**Wall**: `docs/recon/**`
**Status**: `[hw-confirmed]` §1 (HID input-only); `[inferred]` §2 (4× hypothesis refuted by static analysis); `[inferred]` §3 (control path summary)
**Board rows**: #201 (HID input-only, settled), #202 (IR 4× hypothesis, flagged and refuted)
**Related**: #178 (probe), #183 (endpoint map), #194 (frame geometry), #101 (property semantics), #189 (original HID premise, corrected)
