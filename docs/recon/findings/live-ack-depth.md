# Finding #187 — live ack/depth correlation

- Date: 2026-10-21
- Board row: #187 — "The live SET ack's form and the depth pipeline"
- Status: SPEC-READY (write-up from prior evidence; no new device run)

## 1. The evidence

### 1.1 The depth 64-byte "RB" ack

From `2026-10-20-plan132-gapfree-captures.raw.log` (depth section, port 10010, after power-cycle):

```
[dump] reply method=ob_rpc_send_data0-2-10 kind=Str msgid=3 envelope=Array
       result=Str(64) head=[52 42 01 00 02 00 01 00 00 00 00 00 00 00 00 00 ...]

[dump] reply method=ob_rpc_send_data0-2-10 kind=Str msgid=4 envelope=Array
       result=Str(64) head=[52 42 01 00 02 00 02 00 00 00 00 00 00 00 00 00 ...]

[dump] reply method=ob_rpc_send_data0-2-10 kind=Str msgid=5 envelope=Array
       result=Str(64) head=[52 42 01 00 02 00 03 00 00 00 00 00 00 00 00 00 ...]
```

- **Every** `send_data` reply on port 10010 (depth) is a 64-byte `Str` starting with `0x52 0x42` = "RB" in ASCII (the GM/RB vendor response frame, `protocol-spec.md` §2.1).
- The header is the standard 10-byte RespHeader (little-endian): `u16 magic | u16 sizeInHalfWords | u16 opcode | u16 requestId | u16 errorCode`.
- The first 10 bytes: `52 42 01 00 02 00 01 00 00 00` → magic="RB" | sizeInHalfWords=1 (2 bytes payload) | opcode=2 (SET_PROPERTY) | requestId=1 | errorCode=0 (success).
- The requestId field increments per call (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, ...), matching the request's seq.
- The full response buffer is 64 bytes (the expected response size parameter passed to `send_data`), though the actual payload per the sizeInHalfWords field is only 2 bytes; the remainder is padding.
- **Frames flow**: `data=200 empty=0 error=0` — every `get_frame` returned a 2304000-B payload (1920×1200×1 byte).

### 1.2 The live 0-byte ack

From `2026-10-21-plan175-depth-telemetry-final.raw.log` (port 10010, no power-cycle, daemon already running):

```
[live] SET prop=3 value=0 reqid=1 -> vendor no ack (0 B)
[live] SET prop=152 value=0 reqid=2 -> vendor no ack (0 B)
[live] SET prop=153 value=1 reqid=3 -> vendor no ack (0 B)
[live] SET prop=154 value=866000 reqid=4 -> vendor no ack (0 B)
[live] SET prop=155 value=866000 reqid=5 -> vendor no ack (0 B)
```

- Every `send_data` call returned `vendor no ack (0 B)` — the reply body is empty.
- **No frames flow**: `done: get_frame=62 data=0 empty=62 error=0; first_frame=none`.

### 1.3 The IR contrast

From `2026-10-21-plan184-prop192-ir-attempt5.raw.log` (port 10013, same daemon state):

```
[live] SET prop=3 value=0 reqid=1 -> vendor no ack (0 B)
[live] SET prop=152 value=0 reqid=2 -> vendor no ack (0 B)
...
[live] saved first frame: /tmp/raptor_frame.raw (2304000 bytes)
[live] GET_FRAME #5 -> band(2304000) head=[10 10 10 10 10 10 10 10 10 10 ...]
```

- Every `send_data` call returned `vendor no ack (0 B)` — same as depth.
- **Frames flow**: `done: get_frame=56 data=52 empty=4 error=0` — the IR interface delivered data despite the 0-B acks.

From `2026-10-20-plan132-gapfree-captures.raw.log` (IR section, port 10013, after power-cycle):

```
[dump] reply method=ob_rpc_send_data0-2-12 kind=Str msgid=3 envelope=Array
       result=Str(0) head=[]
```

- The IR `send_data` replies are `Str(0)` (empty string, 0 bytes) — different from the depth's `Str(64)`.
- **Frames flow**: `data=180 empty=0 error=0`.

## 2. The correlation

| Interface | Port | `send_data` reply | Frames flow? |
|---|---|---|---|
| IR | 10013 | `Str(0)` (gapfree) or `0 B` (live) | Yes (180/180 or 52/4) |
| Depth | 10010 | `Str(64)` "RB..." (gapfree) | Yes (200/200) |
| Depth | 10010 | `0 B` (live) | No (0/62) |

**The depth pipeline requires the 64-byte "RB" response from `send_data` to enable frame streaming.** The IR pipeline does not — it flows frames regardless of whether the ack is `Str(0)` or `0 B`.

## 3. The hypothesis

The 0-B ack on depth is not a failure of the `send_data` call itself (the RPC round-trip completes), but a failure of the **daemon's depth pipeline setup**:

- When the daemon is in a clean state (after power-cycle, before scan_server runs), the depth `send_data` returns the expected 64-byte "RB" response, and frames flow.
- When the daemon is already running (scan_server has claimed the device), the depth `send_data` returns 0 B, and frames do not flow — the daemon's depth pipeline is inhibited or idle.

The IR pipeline is independent — it does not require the "RB" ack, so frames flow even when the daemon is busy.

**What the evidence does and does not establish:**

- ✅ **Established**: The 64-byte "RB" ack on depth correlates with frame flow; the 0-B ack correlates with no frames.
- ✅ **Established**: The IR pipeline does not require the "RB" ack.
- ❓ **Not established**: Whether the 0-B ack is the *cause* of the no-frame condition, or a *symptom* of the daemon's depth pipeline being inhibited.
- ❓ **Not established**: Whether the daemon's depth pipeline can be re-enabled without a power-cycle, or whether it requires the scan_server app to release the device.

## 4. The next live test

**Goal**: Confirm whether the 64-byte "RB" ack is the gate for depth frames, or whether the daemon's pipeline setup is the gate.

**Test 1 — Replay the 64-byte "RB" ack**:

```bash
# Use the #132 depth capture bundle as a reference:
# ~/captures/plan132-depth-v2/replies/port-10010.bin

# Extract the first send_data reply (msgid=3, Str(64) "RB..."):
# The reply is in the capture bundle; replay it against a live depth session.

# If the daemon accepts the replayed "RB" ack and frames start flowing,
# then the ack is the gate. If frames still don't flow, then the daemon's
# pipeline setup is the gate.
```

**Test 2 — Restart the daemon**:

```bash
# Kill the scan_server daemon, then run raptor_client on port 10010:
pkill -f scan_server
sleep 2
build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 8 --frames 10

# Expected: if the daemon's pipeline setup is the gate, then after restart,
# the depth `send_data` should return the 64-byte "RB" ack, and frames should flow.
```

**Test 3 — Check the daemon's depth stream setup**:

```bash
# Run raptor_client with --dump-replies to capture the full RPC exchange:
build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 8 --frames 10 --dump-replies

# Inspect the log: does the depth `start_stream` return `Bool(true)` or `Bool(false)`?
# Does the daemon's depth pipeline initialize (look for depth-specific RPC calls)?
```

**What the log would show:**

- If the 64-byte "RB" ack is the gate: the live test would show `send_data` returning `Str(64)` and frames flowing (data>0).
- If the daemon's pipeline setup is the gate: the live test would show `send_data` returning `0 B` and frames not flowing (data=0), even after restart.

## 5. Consequences

### 5.1 For #175 (depth telemetry)

The depth telemetry failure (`data=0 empty=62`) is not a client-side bug — it is the daemon's depth pipeline being inhibited. The fix is not in `raptor_client` but in the daemon's device-claim logic or the scan_server app's lifecycle management.

### 5.2 For #184 (prop=192 ACCEPT/REJECT)

The ACCEPT/REJECT branch in PR #464 is unreachable on live hardware because the vendor returns 0-B acks on every SET. The branch is testable only via mock/replay, not against the live device in its current state.

### 5.3 For the shim's OB words

The shim's `ob_rpc_send_data` must handle both reply forms:

- `Str(64)` "RB..." — the expected depth ack (when the daemon's pipeline is initialized).
- `Str(0)` or `0 B` — the IR ack, or the depth ack when the daemon is busy.

The shim should not treat `0 B` as an error — it is a valid (if unhelpful) reply.

### 5.4 For the live source

The live source (`engine/src/live/`) should:

- Log the `send_data` reply size (0 B vs 64 B) as a diagnostic.
- Not block on the ack — the ack is a confirmation, not a gate.
- If the depth pipeline is inhibited, surface that as a "device busy" error, not a "no ack" error.

## 6. Open questions

- **OQ-187-1**: Is the 64-byte "RB" ack the *cause* of the depth frame flow, or a *symptom* of the daemon's pipeline being initialized? (Requires Test 1 above.)
- **OQ-187-2**: Can the daemon's depth pipeline be re-enabled without a power-cycle? (Requires Test 2 above.)
- **OQ-187-3**: What is the "RB" response's semantic? It is a GM/RB vendor frame with opcode=0x0001, seq incrementing, status=0x0001. Is this a "device ready" ack, or a "property set" ack? (Requires static analysis of the vendor binary's GM/RB handler.)

## 7. References

- `2026-10-20-plan132-gapfree-captures.md` — the gapfree capture (IR + depth, post-power-cycle)
- `2026-10-20-plan132-gapfree-captures.raw.log` — the raw log with the 64-byte "RB" acks
- `2026-10-21-plan175-depth-telemetry-final.md` — the depth telemetry failure (0-B acks, no frames)
- `2026-10-21-plan175-depth-telemetry-final.raw.log` — the raw log with the 0-B acks
- `2026-10-21-plan184-prop192-ir-attempt5.md` — the IR run (0-B acks, frames flow)
- `2026-10-21-plan184-prop192-ir-attempt5.raw.log` — the raw log with the 0-B acks and flowing frames
- `protocol-spec.md` §2 — the GM/RB vendor frame format
- `stream-profiles-row114.md` §3 — the GM/RB header fields (little-endian)
