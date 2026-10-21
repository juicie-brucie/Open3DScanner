# Frame Band Rule — Row #174

*Static analysis of #132 gap-free captures (2026-10-20). `[hw-confirmed: plan132-gapfree-captures]`*

## 1. The Question

Does the daemon return image data in ~12 KB bands that require client-side assembly (as claimed by #124/#131), or does it return whole frames in a single reply?

## 2. The Evidence — #132 Gap-Free Captures

The #132 hardware run captured complete RPC reply streams from both IR and depth interfaces post-power-cycle, with no BPF filter restrictions.

### 2.1 IR Interface (port 10013)

- **180 frames captured**, all with payload size **Bin(2304000)**
- 2304000 bytes = 1920 × 1200 × 1 byte = **the whole frame**
- 0 empty replies, 0 errors
- Monotonically increasing timestamps (e1 field)
- Bundle: 1078 records, 414,792,579 bytes

### 2.2 Depth Interface (port 10010)

- **200 frames captured**: 199 with Bin(2304000), 1 with Bin(1321920)
- The partial frame (Bin(1321920)) is at **msgid=914**, which is the **152nd GET_FRAME** call
  - *Correction to gapfree-captures.md*: the record incorrectly labeled this as the "first frame"
- 2304000 bytes = 1920 × 1200 × 1 byte = **the whole frame**
- 0 empty replies, 0 errors
- Monotonically increasing timestamps (e1 field)
- Bundle: 1202 records, 459,963,865 bytes

### 2.3 The Partial Frame — Bin(1321920)

The depth interface returned one partial frame:
- **Size**: 1321920 bytes = 688.5 rows (1321920 / 1920 = 688.5)
- **Location**: msgid=914 (152nd GET_FRAME call, not the first)
- **Content**: Raw 8-bit pixels, same format as full frames
- **Interpretation**: This is a partial frame capture, likely due to timing or buffering at the daemon side. It is NOT evidence of a band-based protocol.

## 3. What #124/#131 Actually Measured

### 3.1 The Capture Filter Artifact

#124/#131 analyzed `session4.pcapng` from 2026-10-18 with a BPF filter:
```
... and less 16000
```

This filter **dropped all TCP segments ≥ 16 KB**. The actual frame payloads are 2.3 MB, so they were completely dropped by the filter.

### 3.2 The "12 KB Band" Misinterpretation

The extractor (frame_bins.py) found:
- 30 depth frames, max 12,276 bytes
- 3 IR frames, max 12,276 bytes

**These were NOT logical bands.** They were TCP segment fragments that happened to fit the `< 16000` filter. The extractor misinterpreted these fragments as the actual payload size.

### 3.3 The Correct Interpretation

The daemon does **not** split frames into ~12 KB bands. It sends **whole frames** (2.3 MB) in a single RPC reply. The #124/#131 claim was based on incomplete data due to the capture filter.

## 4. The Band Rule — Verdict

**The band rule does NOT survive.**

- There is no band-based protocol
- The daemon returns whole frames (2.3 MB) in a single reply
- No client-side band assembly is needed
- The ~12 KB claim was a capture artifact, not a protocol feature

## 5. The Frame Container Structure

The RPC reply structure (from #132 captures, confirmed by #124 envelope analysis):

```
msgpack array(11):
  [0] UInt(0)                    - always 0
  [1] UInt(timestamp_us)         - frame timestamp in microseconds
  [2] UInt(counter)              - secondary counter/timestamp
  [3] UInt(255)                  - format field (OBFormat = 255)
  [4] Bin(2304000)               - frame payload: 1920×1200×1 raw 8-bit pixels
  [5] UInt(2304000)              - payload length
  [6] Bin(0)                     - secondary buffer (empty)
  [7] UInt(0)                    - secondary buffer length
  [8] Bin(6)                     - tertiary buffer (6 bytes, unknown purpose)
  [9] UInt(6)                    - tertiary buffer length
  [10] UInt(3 or 4)              - IR light type (3 or 4, varies per frame)
```

The frame payload (element [4]) is **headerless raw 8-bit pixels** with 1920-byte row stride (confirmed by #124 autocorrelation analysis at lag 1920).

## 6. Open Questions

### 6.1 Pixel Scale — Row #126

The pixel values are 8-bit single-channel (0-255), but the **physical meaning** is not settled:
- Are these depth values in millimeters?
- Are these normalized intensity values (0-255)?
- Is there a calibration curve to convert to physical units?

This is row #126's responsibility. The #132 captures provide the raw data but not the calibration context.

### 6.2 The Partial Frame — Bin(1321920)

Why did the depth interface return one partial frame (688.5 rows) at msgid=914?
- Timing issue at the daemon?
- Buffer underrun?
- Intentional partial delivery?

This is not critical for the protocol understanding, but it's an anomaly worth noting.

## 7. Implications for C2 (Phase B)

The frame delivery is **simple**: one RPC reply = one whole frame. No band assembly logic is needed. The client must:
1. Call `ob_rpc_get_frame` in a loop
2. Parse the 11-element array
3. Extract element [4] as the frame payload
4. Interpret as 1920×1200 raw 8-bit pixels (row-major, 1920-byte stride)

The pixel scale interpretation (row #126) is a separate concern.

## 8. Summary

| Question | Answer | Confidence |
|----------|--------|------------|
| Does the daemon use ~12 KB bands? | **No** — it sends whole 2.3 MB frames | hw-confirmed |
| Is client-side band assembly needed? | **No** | hw-confirmed |
| What is the frame format? | Raw 8-bit pixels, 1920×1200, 1920-byte row stride | hw-confirmed |
| What is the pixel scale? | **Unknown** — row #126 | open |
| Why one partial frame (Bin(1321920))? | Unknown — anomaly, not protocol feature | open |

---

**Wall**: `docs/recon/**`
**Status**: `[hw-confirmed: plan132-gapfree-captures]` — verified by gap-free hardware captures
**Supersedes**: #124/#131 band claim (capture artifact, not protocol feature)
**Related**: #126 (pixel scale), #131 (frame container structure)
