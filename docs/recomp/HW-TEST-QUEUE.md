# Hardware test queue — 2026-10-20

Owner: dispatch. Runner: the **hardware tester (Claude)** with the operator. This is the
ready-to-run backlog; the operator decides when the scanner and the room are available. The
operator owns the scanner, its placement and the timing. Read this from `origin/main` in the
hardware worktree (`~/DevReposiMac/Open3DScanner-wt-hw`); do not edit it.

## Current state

- `#157` (suffix) is **`DONE`**: the 2026-10-20 #127 retry proved it against the real daemon on
  IR — 33 `get_frame` calls, **30 data / 3 empty / 0 errors**, each data reply `Bin(2304000)`.
  The T1a replay is banked; re-run T1 only if the binary changes.
- `#153`'s exposure half is confirmed (`exposure_mean=0.00 ms`, `exposure_applies_total=0` over
  1344 ticks); its **picture-unchanged** visual and **#76**'s coverage/red overlay ride the T2
  UI-open A/B. **#160** (the coverage update trigger) landed in #413; #76's acceptance still
  needs that live run.
- **#167 — the device-enumeration race — is intermittent, not every start.** Five occurrences
  are now recorded: the first at 06:23 (`plan153` attempt 1), then 17:39:58, 17:58:09, 18:00:59
  and 18:08:15 — each logs `DeviceManager list: (1)` → `count=0` → **SIGSEGV 11** in the SDK
  teardown. Two clean starts (17:00:47 `plan153`, 17:29:57 `plan76`) sit in the same window, so
  a clean start is not evidence the race is gone. **Do not schedule T2 until #167 lands**;
  build1 owns the bounded retry.
- **#161** is the depth-interface timeout (landed, PR #416); its live depth retry is the
  hardware card. **#168** is the `--capture` path hang (IR, 2-for-2, both exit 143 with zero
  output); build2 owns it. **Do not run T3's depth/capture until #168 lands.**
- The old untracked batch is no longer the #392 HOLD: #392 landed the #71 attempt 5/#123/
  ungated-2 records; the newest crash/hang records are a separate, unlanded batch (provenance
  gaps).

## Rules for every run

- One SDK user: no second `scan_server` / `raptor_client` / vendor client.
- `scripts/hw-preflight.sh` first; `scripts/hw-postflight.sh` after every run.
- One record per run, from `tests/results/_TEMPLATE.md`; raw log beside it.
- **Record checklist (P12-lite — the factory is not built yet):**
  - exact command; run start; binary path **and sha256**; the other copy + mtime;
  - raw log path **and sha256**; capture bundle manifest if used;
  - device count / one-SDK-user check; daemon state; exit code; crash sweep line;
  - client count **and UI-open state** beside every perf number;
  - the operator's visual read, with the recording name + segment, **marked attributed**;
  - recommended board move — **do not edit `PLAN.md`**.
- No build lands mid-session. Freeze builds at the start of T1/T2.
- The operator's visual read is evidence by attribution; do not re-analyse the recording.

---

## T1 — The room floor (run first)

**Rows:** #157 (daemon check), #105 live half, #127 live half, #98 acceptance, #151 raw rate,
#132 input.
**Setup:** macOS room; daemon running; device `count=1`; one SDK user; the current `main`
binary; no build changes during the session.

**T1a — replay (no live dependency; bank this first):**

```
./build/tools/raptor_client/raptor_client --replay docs/recon/findings/streaming-replay-payloads.hex \
  127.0.0.1 10013 --dump-replies --capture ~/captures/floor-replay
```

Acceptance: the replay completes; the per-port `band(N)` / `empty` tally is in the raw log;
no parser error.

**T1b — live IR (the pivot's proof):**

```
./build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 30 \
  --capture ~/captures/floor-ir --frames 200 --dump-replies
```

Acceptance: the **first RPC is accepted on the suffixed method**
(`ob_rpc_start_stream0-2-12`); every `get_frame` reply is `band(N)` or `empty` — never an
error, never no reply; the reply's element kinds are named by `--dump-replies`.

**Stop rule:** if the IR run fails at the first RPC, record the named rejection and **cancel
T3's depth run**; keep T2 (different binary path). Both outcomes are results.

**Records:** one for T1a, one for T1b. Name the port, frame/reply count, reply rate, binary sha.

---

## T2 — The UI-open A/B (the follow-up to the 2026-10-20 data)

**Blocked on #167** — do not run this until the device-enumeration race is fixed. The race is
intermittent (two clean starts sit in the same 17:xx window), so a start that succeeds does
not clear the block. Once it is,
this run closes #153's picture, #76's coverage/red overlay, #93's markerless HUD, #141's UI half,
#151's fused rate and #71's A/B. #160's coverage fix is already on `main`.

**Rows:** #71 (single-session A/B), #153 (picture unchanged), #76 (coverage/red overlay), #93
(markerless HUD), #141 UI half, #151 fused rate.
**Setup:** macOS room; a **still scene** (scanner at rest; the operator confirms the scene does
not move); the UI open in a browser; one SDK user; no build changes.

```
./build/tools/scan_server/scan_server --live 8787
# browser: http://127.0.0.1:8787/
```

**A/B sequence (one session, static scene):**

1. Start with the plane toggles **off** (no `OPENSCANNER_PLANE_REMOVE`). Let it run ~30 s.
2. Send `{"cmd":"set_plane_removal","auto":true}` over the WebSocket; run ~30 s.
3. Send `{"cmd":"set_plane_removal","auto":false}`; run ~30 s.
4. Pause/finish normally; keep the post-pause mesh.

Minimal WS sender (no third-party package; the WS URL is the server root):

```python
import base64, os, socket
host, port = "127.0.0.1", 8787
msg = b'{"cmd":"set_plane_removal","auto":true}'
s = socket.create_connection((host, port), timeout=5)
key = base64.b64encode(os.urandom(16)).decode()
s.sendall((f"GET / HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
           f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
           f"Sec-WebSocket-Version: 13\r\n\r\n").encode())
resp = b""
while b"\r\n\r\n" not in resp:
    resp += s.recv(4096)
assert b" 101 " in resp.split(b"\r\n")[0]
mask = os.urandom(4)
payload = bytes(b ^ mask[i % 4] for i, b in enumerate(msg))
s.sendall(bytes([0x81, 0x80 | len(msg)]) + mask + payload)
s.close()
```

Repeat with `"auto":false`. The server logs each transition with the tick and the last
published point count; align those ticks with the point-count series.

**The operator's visual reads (attributed):**

- **#76:** the HUD's `coverage` line and the red under-covered overlay on the live cloud.
- **#93:** a markerless stretch; the HUD tracking line reads `LOST, LOST, MARKERLESS` with
  **no** `markerless window k/3` suffix.
- **#153:** the picture is unchanged across the session while the exposure/laser config is not
  re-asserted (the log shows one pre-start push); record the screen recording and segment.
- **#141:** the `[perf]` line with the client count and the UI-open state beside every number.
- **#151:** the `FRAME_READY`/s from the log (the fused rate).

**Claude records:** the toggle ticks and point-count series; the `[perf]` summary; the
`FRAME_READY`/s; exit code; crash sweep. The visual reads are the operator's, attributed.

**Acceptance / row moves:** the #71 A/B observation; #153's visual; #76's coverage; #93's
markerless line; #141's UI half; #151's fused rate. If a row's acceptance is met, recommend
the move; the dispatcher applies it.

---

## T3 — Depth (blocked on #161 and #168)

**Do not run the depth or `--capture` path until both land:** #161 is the RPC timeout (build3),
#168 is the `--capture` startup hang (IR, 2-for-2). Once both are fixed, this run feeds #132 and
then #143/#124's remaining clauses/#159's capture.

**Row:** #132 (the gap-free capture / band rule / pixel scale), then #143, #124's remaining
clauses, #159's capture.
**Command:** one interface per run:

```
./build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 30 \
  --capture ~/captures/plan132-depth --capture-seconds 30 --capture-bytes 500000000
```

**Stop rule:** if the IR floor failed at the first RPC, cancel this run (same path, same
defect). Record the capture bundle manifest.

---

## T4 — The operator capture session (#145 + #150, one sitting)

**Rows:** #145 (three modes + pause/resume + exposure slider + power-on; the mode-name →
profile-row join), #150 (IMU axis order, temperature unit, gyro; OQ-150-1…6).
**Setup:** operator + device + vendor client; a separate sitting from T1/T2/T3; one capture
directory and bundle.

Checklist, in order:

1. Switch the three scanning modes with `lo0`/RPC traffic; at each switch read **4034 then
   4035** (the named mode table and the profile rows; see `findings/small-segments-row144.md`).
2. Pause/resume; the exposure slider; a power-on.
3. ~2 minutes still (temperature drift), a 90° rotation about the table normal (axis order), a
   fast rotation (the gyro's inertness).

Record: the RPC/IMU capture, the bundle manifest, the operator's notes. This closes the
mode-name join and OQ-150-1…6; it does **not** merge with T5's known scene.

---

## T5 — The known scene (#126, after the ground-truth read)

**Rows:** #126 (controlled content), #159 (the real retention curve).
**Precondition:** the **ground-truth frame** read is owed (which frame `model.ply` is in — the
rotate-pose question). Do that static read first; then the operator sets a flat wall at
300/400/500 mm and a checkerboard, and the run captures raw bins via `raptor_client`, decoded
with #125's tool. This is a different physical setup from T4.

---

## T6 — Windows cross-machine (#115)

**Row:** #115 (the M1 / M2 Air / Windows live-rate comparison).
**Host:** the Windows 5900X / RTX 2080 Ti / 32 GB machine with the scanner attached.
**Checks:** the vendor install state; `scan_server <port>` non-device smoke (synthetic is the
default mode); `raptor_client --discover`; the daemon's raw frame rate vs the synthetic
server's. The Windows dispatcher/team is the peer; the parent's contract is the one header.
No cross-repo edit.

---

## Order and stop rules, in one line

**T1a → T1b → T2 → (T3 only if T1b passed) → T4 (separate) → T5 (after the ground-truth read)
→ T6 (when the scanner is on Windows).** One interface per live run; no build mid-session; a
failed IR floor cancels T3 but not T2.
