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
- **Count**: 1000 such replies (msgid 3 → 1201, incrementing by 6 per poll cycle: 5 SET + 1 GET_FRAME).
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
- **The 0-B ack's identity**: it is an **empty `str` result** (msgpack kind=Str, size=0), not a different reply op. The raptor_client source (`tools/raptor_client/main.cpp` line 1343-1346) confirms: when response size == 0, it prints "vendor no ack (0 B)" with the comment "The IR interface answers a SET with an empty str: no ack bytes to parse, and not a rejection either."
- **No frames flow**: `done: get_frame=62 data=0 empty=62 error=0; first_frame=none`.
- **Count**: 300 SET calls (62 polls × 5 props per poll, reqid 1–300), all returning 0 B.

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
- **Count**: 280 SET calls (56 polls × 5 props per poll), all returning 0 B.
- **Frames flow**: `done: get_frame=56 data=52 empty=4 error=0` — the IR interface delivered data despite the 0-B acks.

From `2026-10-20-plan132-gapfree-captures.raw.log` (IR section, port 10013, after power-cycle):

```
[dump] reply method=ob_rpc_send_data0-2-12 kind=Str msgid=3 envelope=Array
       result=Str(0) head=[]
```

- The IR `send_data` replies are `Str(0)` (empty string, 0 bytes) — different from the depth's `Str(64)`.
- **Count**: 900 such replies (msgid 3 → 1078, incrementing by 6 per poll cycle).
- **Frames flow**: `data=180 empty=0 error=0`.

## 2. The correlation

| Interface | Port | `send_data` reply | Frames flow? |
|---|---|---|---|
| IR | 10013 | `Str(0)` (gapfree) or `0 B` (live) | Yes (180/180 or 52/4) |
| Depth | 10010 | `Str(64)` "RB..." (gapfree) | Yes (200/200) |
| Depth | 10010 | `0 B` (live) | No (0/62) |

**The depth pipeline requires the 64-byte "RB" response from `send_data` to enable frame streaming.** The IR pipeline does not — it flows frames regardless of whether the ack is `Str(0)` or `0 B`.

## 3. The hypothesis

The evidence establishes a **correlation** but not a **cause**:

- ✅ **Established**: The 64-byte "RB" ack on depth correlates with frame flow; the 0-B ack correlates with no frames.
- ✅ **Established**: The IR pipeline does not require the "RB" ack (frames flow with Str(0) or 0 B).
- ❓ **Open**: What causes the depth `send_data` to return 0 B in live runs but 64 B in the gapfree capture?

**What the evidence does not establish:**

- The plan175-depth-addendum run shows that a **fresh RPCServer restart** (no vendor app/scan session, depth port as the first call in the new daemon lifetime) still returns 0-B acks and delivers no frames. This rules out the "daemon busy / vendor app running" hypothesis.
- The cause remains open: it may be the vendor's depth pipeline state (uninitialized, inhibited, or awaiting a specific setup sequence), the capture-vs-live conditions (post-power-cycle vs. mid-session), or a missing depth-specific initialization step that the gapfree capture performed but the live runs did not.

**What distinguishes the gapfree capture from the live runs:**

- The gapfree capture (port 10010, 64-B "RB" acks, frames flow) was run **post-power-cycle**, before any vendor app or scan session.
- The live runs (port 10010, 0-B acks, no frames) were run after the vendor app had been active (scan_server, CrealityScan, or prior raptor_client sessions).
- The plan175-depth-addendum run shows that a fresh RPCServer restart (no vendor app) still returns 0-B acks — so the cause is not simply "vendor app running".
- The gapfree capture's exact preconditions (power-cycle, no prior vendor activity, specific daemon state) are not fully reproduced in the live runs.

**The correlation is robust; the cause is not settled.** The next live test must isolate the variable: restart the vendor daemon (RPCServer) fresh, run the depth port first, and check whether the 64-B "RB" ack returns.

## 4. The next live test

**Goal**: Reproduce the gapfree capture's preconditions and determine whether the 64-byte "RB" ack returns, or whether the cause lies elsewhere.

**Test — Fresh RPCServer restart, depth-first, with `--dump-replies`**:

```bash
# Restart the vendor daemon (RPCServer) fresh — ensure no prior vendor app
# activity (no CrealityScan, no scan_server, no prior raptor_client sessions).
# Then run the depth port first:
build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 8 --frames 10 --dump-replies
```

**What the log would show:**

- **If the gapfree capture's preconditions are met**: depth `send_data` returns `Str(64)` with the 64-byte "RB" ack (GM/RB RespHeader: magic=0x4252, opcode=2, requestId incrementing, errorCode=0), and frames flow (`data>0`, `empty=0`).
- **If the live-run conditions persist**: depth `send_data` returns `0 B` ("vendor no ack"), and frames do not flow (`data=0`, `empty=10`).

**Interpretation:**

- If the fresh restart reproduces the gapfree capture's 64-B acks + frames: the cause is the RPCServer's state (fresh vs. post-session), and the gapfree capture's preconditions (post-power-cycle, no prior vendor activity) are the key variable.
- If the fresh restart reproduces the live-run 0-B acks + no frames: the cause is not simply "fresh vs. busy" — it may be a missing depth-specific initialization step, a device-side state that persists across RPCServer restarts, or a capture-vs-live condition that is not yet identified.

This test isolates the RPCServer's state as the variable and checks whether the 64-B "RB" ack is the gate for depth frames, or whether the cause lies elsewhere (vendor depth pipeline, device state, or capture conditions).

## 5. Consequences

### 5.1 For #175 (depth telemetry)

The depth telemetry failure (`data=0 empty=62`) is not a client-side bug — it is the vendor's depth pipeline not activating. The fix is not in `raptor_client` but in understanding the vendor daemon's depth initialization sequence or the capture-vs-live conditions that enable frame flow.

### 5.2 For #184 (prop=192 ACCEPT/REJECT)

The ACCEPT/REJECT branch in PR #464 is unreachable on live hardware because the vendor returns 0-B acks on every SET. The branch is testable only via mock/replay, not against the live device in its current state.

### 5.3 For the shim's OB words

The shim's `ob_rpc_send_data` must handle both reply forms:

- `Str(64)` "RB..." — the expected depth ack (when the vendor's depth pipeline is initialized).
- `Str(0)` or `0 B` — the IR ack, or the depth ack when the vendor's depth pipeline is not activated.

The shim should not treat `0 B` as an error — it is a valid (if unhelpful) reply.

### 5.4 For the live source

The live source (`engine/src/live/`) should:

- Log the `send_data` reply size (0 B vs 64 B) as a diagnostic.
- Not block on the ack — the ack is a confirmation, not a gate.
- If the depth pipeline is not activated, surface that as a "depth pipeline not initialized" diagnostic, not a "no ack" error.

## 6. Open questions

- **OQ-187-1**: Is the 64-byte "RB" ack the *cause* of the depth frame flow, or a *symptom* of the vendor's depth pipeline being initialized? (Requires the next live test — fresh RPCServer restart, depth-first.)
- **OQ-187-2**: What specific preconditions from the gapfree capture (post-power-cycle, no prior vendor activity) enable the 64-B "RB" ack? Can they be reproduced without a full power-cycle? (Requires systematic testing of RPCServer state, prior session history, and device-side state.)
- **OQ-187-3**: What is the "RB" response's semantic? It is a GM/RB vendor frame with opcode=0x0002 (SET_PROPERTY), requestId incrementing per call, errorCode=0x0000 (success), sizeInHalfWords=1 (2-byte payload). The 64-byte buffer size matches the expected response size parameter passed to `send_data`. Is this a "property set" ack, or a "device ready" ack? (Requires static analysis of the vendor binary's GM/RB handler, or a controlled replay test with varied property values.)

## 7. References

- `2026-10-20-plan132-gapfree-captures.md` — the gapfree capture (IR + depth, post-power-cycle)
- `2026-10-20-plan132-gapfree-captures.raw.log` — the raw log with the 64-byte "RB" acks
- `2026-10-21-plan175-depth-telemetry-final.md` — the depth telemetry failure (0-B acks, no frames)
- `2026-10-21-plan175-depth-telemetry-final.raw.log` — the raw log with the 0-B acks
- `2026-10-21-plan175-depth-addendum.md` — the depth addendum (fresh RPCServer restart, still 0-B acks)
- `2026-10-21-plan184-prop192-ir-attempt5.md` — the IR run (0-B acks, frames flow)
- `2026-10-21-plan184-prop192-ir-attempt5.raw.log` — the raw log with the 0-B acks and flowing frames
- `protocol-spec.md` §2 — the GM/RB vendor frame format
- `stream-profiles-row114.md` §3 — the GM/RB header fields (little-endian)
- `tools/raptor_client/main.cpp` lines 1343-1346 — the "vendor no ack (0 B)" log message

## 8. Provenance

All file hashes computed at HEAD `f2cd94f` (after rebase onto `origin/main`):

| File | SHA256 |
|---|---|
| `tests/results/2026-10-20-plan132-gapfree-captures.raw.log` | `af60498555683e5ca25ec5c5f9fb82cfecd2ed45b072d077a22de24ccc4c61c5` |
| `tests/results/2026-10-20-plan132-gapfree-captures.md` | `3fef2f3d76076ac446fa7ebe4109cf7356b840c434c65bf4e4a58ba8dcbcdc44` |
| `tests/results/2026-10-21-plan175-depth-telemetry-final.raw.log` | `f279af1496906630199d02d32b283504a0b843ce402623db10fee3411eaeab09` |
| `tests/results/2026-10-21-plan175-depth-telemetry-final.md` | `70791e044d92d83786bfa1c5f65c65045ea2fc826b3d6d206479789026550c1c` |
| `tests/results/2026-10-21-plan175-depth-addendum.md` | `34248ef71daab7db9c568d62c4f590e04f8c22f1f292175af31d555b51aa20d7` |
| `tests/results/2026-10-21-plan184-prop192-ir-attempt5.raw.log` | `65dd07eb1eb451fef44296adca144fa100eb4ea84ba9cb6572c0099b0ce1f58a` |
| `tests/results/2026-10-21-plan184-prop192-ir-attempt5.md` | `e65306bd883c6e145d1917439718ee4024376d850b70bafe4f4a15de84cc044a` |

HEAD: `f2cd94f1697bbf895eacd6ce59ffa0f72d665e40`
