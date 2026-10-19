# Row #101 / OQ-F2: Property Semantics and Method Suffixes

*Observation-derived from session4 capture (2026-10-18). `[inferred]` — no vendor binary, no gated run. Captures stay outside repo.*

## 1. Summary

This finding documents the vendor's property usage patterns extracted from the session4 capture: the laser control loop cadence and ordering, the exposure/current ramp behavior, the method suffix encoding, and the property value domains. The analysis is incomplete—many properties are carried but their semantics are unknown—but the two host-driven loops (laser and exposure) are fully characterized.

## 2. Method Suffix Encoding

**All property operations target port 10010 (depth interface, suffix `0-2-10`).**

The method name format is `ob_rpc_<method><device><interface>`, where:
- Device: `0-2-1` (the Raptor Pro)
- Interface index: `0` (depth), `2` (IR), `4` (RGB), `6` (IMU)
- Port mapping: 10010 (depth), 10011 (IMU), 10012 (RGB), 10013 (IR)

In the session4 capture:
- 2,869 SET_PROPERTY calls: all on port 10010
- 267 GET_PROPERTY calls: all on port 10010

**Conclusion**: The vendor client only queries and modifies properties on the depth interface during streaming. Properties on other interfaces (IR, RGB, IMU) are either not used during scanning or are accessed through different mechanisms (e.g., `ob_set_ir_frame_light_type` on port 10013 uses a different method, not the generic property API).

**OPEN QUESTION**: Whether properties are interface-specific or global (i.e., whether setting prop X on depth also affects IR/RGB). Cannot settle from this capture.

## 3. The Two Host-Driven Loops

### 3.1 Laser Control Loop (props 3, 152, 153)

**Pattern**: Strict sequence `[3, 152, 153]` repeating at ~32ms cadence.

**Observations**:
- 892 calls to prop 3, 893 to prop 152, 893 to prop 153
- Values: binary 0 or 1 for all three
- Cadence: min 1.2ms, max 13,957ms, avg 32.2ms
- Sequence is always 3→152→153, never out of order
- Loop pauses during scan interruptions (gaps up to 14s observed)

**Interpretation**:
- **Prop 3**: Laser on/off flag (1=on, 0=off)
- **Prop 152**: Laser group selector (0 or 1 — possibly selects which laser array or pattern)
- **Prop 153**: Laser mode or intensity flag (0 or 1 — possibly high/low power mode)

**Rationale**: The tight 32ms cadence and binary values suggest real-time laser control during scanning. The three-property sequence likely sets: (1) enable laser, (2) select laser group/pattern, (3) set mode/intensity. The loop stops when scanning pauses, confirming these are active-scan controls.

### 3.2 Exposure/Current Ramp (props 154, 155)

**Pattern**: Props 154 and 155 are always set together with identical float values, ramping up over time.

**Observations**:
- 27 calls to each prop
- Values are float bit patterns (IEEE 754 single precision)
- Both props receive the same value in each call
- Ramp sequence: 500000.0 → 800000.0 → 866000.0 → 932000.0 → ... → ~1.5e6
- Cadence: avg 1,517ms between calls (much slower than laser loop)

**Interpretation**:
- **Prop 154**: Exposure time or integration time (float, in microseconds or some time unit)
- **Prop 155**: Laser current or intensity (float, in mA or some current unit)

**Rationale**: The identical values and synchronized updates suggest these are coupled parameters. The ramp-up pattern (starting at 500k, increasing to ~1.5M) matches an auto-exposure or auto-intensity algorithm that gradually increases exposure/current until the image quality is sufficient. The 1.5s cadence is consistent with a slow feedback loop that waits for frame data before adjusting.

**OPEN QUESTION**: Whether the units are microseconds/milliamps or some other scale. Cannot settle without a known reference point (e.g., a frame with known exposure settings).

## 4. Property Map (Deepened)

### 4.1 Laser Control (binary flags)

| Prop | SET calls | Values | Interpretation | Confidence |
|------|-----------|--------|----------------|------------|
| 3    | 892       | 0, 1   | Laser on/off   | HIGH       |
| 152  | 893       | 0, 1   | Laser group selector | MEDIUM |
| 153  | 893       | 0, 1   | Laser mode/intensity | MEDIUM |

**Evidence**: Tight 32ms loop, binary values, stops during scan pauses.

### 4.2 Exposure/Current (float ramp)

| Prop | SET calls | Value range (float) | Interpretation | Confidence |
|------|-----------|---------------------|----------------|------------|
| 154  | 27        | 500k → 1.5M         | Exposure time | MEDIUM     |
| 155  | 27        | 500k → 1.5M         | Laser current | MEDIUM     |
| 5    | 23        | 80k → ...           | Carried, meaning unknown | LOW |
| 240  | 0         | GET cur=8609058816  | Carried, meaning unknown | LOW |

**Evidence**: Props 154/155 always set together, identical values, ramp pattern.

### 4.3 Flags and Enums

| Prop | SET calls | Values | Interpretation | Confidence |
|------|-----------|--------|----------------|------------|
| 151  | 8         | 1      | Carried, meaning unknown | LOW |
| 157  | 8         | 0      | Carried, meaning unknown | LOW |
| 158  | 13        | 0, 1   | Carried, meaning unknown | LOW |
| 183  | 8         | 0, 1   | Carried, meaning unknown | LOW |
| 192  | 7         | 0, 1   | Carried, meaning unknown | LOW |
| 235  | 1         | 1      | Carried, meaning unknown | LOW |

**Evidence**: Binary values, low call count, no clear pattern.

### 4.4 Sliders and Ranges

| Prop | SET calls | Values | Interpretation | Confidence |
|------|-----------|--------|----------------|------------|
| 160  | 1         | 100    | Carried, meaning unknown (GET range: 0-65536) | LOW |
| 161  | 20        | 3, 5, 6, 7, 9, 12, 14, 16, 18, 20, ... | Slider-like (GET range: 0-65536) | LOW |
| 2026 | 19        | 2350-3000 | Carried, meaning unknown (GET range: 0-65536) | LOW |

**Evidence**: Multiple unique values, but no clear semantic mapping.

### 4.5 Enums

| Prop | SET calls | Values | Interpretation | Confidence |
|------|-----------|--------|----------------|------------|
| 164  | 16        | 0, 1, 2, 3, 4, 5 | Enum (6 states) | LOW |

**Evidence**: Small set of discrete values, but no semantic mapping.

### 4.6 Initialization Properties

| Prop | SET calls | Values | Interpretation | Confidence |
|------|-----------|--------|----------------|------------|
| 85   | 2         | 0      | Initialization flag | LOW |
| 89   | 2         | 0      | Initialization flag | LOW |
| 81   | 1         | 0      | Initialization flag | LOW |
| 2019 | 1         | 0      | Initialization flag | LOW |
| 2020 | 3         | 0, 1   | Initialization mode | LOW |
| 2022 | 1         | 8      | Initialization parameter | LOW |
| 2024 | 1         | 2      | Initialization parameter | LOW |

**Evidence**: Low call count, early in session, constant values.

### 4.7 Single-Call Properties

| Prop | SET calls | Value | Interpretation |
|------|-----------|-------|----------------|
| 38   | 1         | 1     | Carried, meaning unknown |
| 39   | 1         | 1     | Carried, meaning unknown |

**Evidence**: No pattern, no semantic mapping.

## 5. Capture List for Disambiguation

To settle the semantics of the unknown properties, the operator must record the following single-action captures:

### 5.1 Pause/Resume Capture

**Action**: Start scanning, pause for 10 seconds, resume scanning.

**Expected observations**:
- Laser loop (props 3, 152, 153) should stop during pause and resume after
- Exposure ramp (props 154, 155) should pause and possibly reset or continue from current value

**Settles**:
- Confirms props 3/152/153 are active-scan controls
- Clarifies whether exposure ramp continues or resets

### 5.2 Mode Switch Capture

**Action**: Switch between scan modes (e.g., "fast scan" vs "high quality scan") if the UI offers such options.

**Expected observations**:
- Props 164 (6-state enum) may change
- Props 160, 161 (sliders) may change
- Laser loop cadence may change
- Exposure ramp target may change

**Settles**:
- Semantic mapping for prop 164 (mode selector?)
- Whether props 160/161 are scan quality parameters

### 5.3 Exposure Slider Capture

**Action**: Manually adjust the exposure/intensity slider in the UI (if available).

**Expected observations**:
- Props 154/155 should change immediately
- Other properties may change if coupled

**Settles**:
- Confirms props 154/155 are exposure/current
- Clarifies units (if the UI shows a known value like "100ms exposure")

### 5.4 Laser Power Slider Capture

**Action**: Manually adjust the laser power slider in the UI (if available).

**Expected observations**:
- Props 3/152/153 values may change (if the slider affects the binary flags)
- Props 154/155 may change (if laser power is coupled with exposure)

**Settles**:
- Semantic mapping for props 3/152/153
- Whether laser power is controlled via flags or float parameters

### 5.5 Resolution Switch Capture

**Action**: Switch scan resolution (e.g., from "high" to "medium" to "low").

**Expected observations**:
- Props 160/161 may change
- Prop 2026 may change
- Frame dimensions may change (observable in get_frame responses)

**Settles**:
- Semantic mapping for props 160/161/2026

## 6. Open Questions

1. **Property scope**: Are properties interface-specific (depth-only) or global?
   - *Needs*: Set a property on IR interface, check if depth interface sees the change.

2. **Float units**: What are the units for props 154/155 (exposure/current)?
   - *Needs*: A known reference point (e.g., UI shows "100ms exposure" while prop 154 = X).

3. **Laser control semantics**: What do props 152/153 select (group, mode, intensity)?
   - *Needs*: Laser power slider capture (5.4).

4. **Enum semantics**: What do the 6 states of prop 164 represent?
   - *Needs*: Mode switch capture (5.2).

5. **Initialization sequence**: What do props 85/89/81/2019/2020/2022/2024 initialize?
   - *Needs*: Power-on capture (record from device boot to first scan).

6. **Coupled parameters**: Are any properties coupled (e.g., does changing exposure also change laser current)?
   - *Needs*: Exposure slider capture (5.3) with careful observation of all property changes.

7. **GET response format**: The GET_PROPERTY response format observed in the captures does not match the expected [cur, max, min, def, step] structure. The values are either zero or very large, suggesting either a different encoding or a parsing error.
   - *Needs*: Manual inspection of raw GET response bytes to verify format.

## 7. Conclusion

The session4 capture fully characterizes the two host-driven loops (laser control at 32ms cadence, exposure ramp at 1.5s cadence) and establishes that all property operations target the depth interface. The property map is partially complete: props 3/152/153 are laser controls, props 154/155 are exposure/current, but the semantics of most other properties remain unknown. The capture list in §5 provides a path to disambiguate the remaining properties through targeted single-action recordings.

**Wall**: `docs/recon/**`
**Status**: `[inferred]` — observation-derived, no vendor binary, no gated run.
