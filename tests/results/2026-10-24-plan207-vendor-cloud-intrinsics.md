# #207 / #208 — paired vendor-cloud recording + the real intrinsics — PASS (recording), BLOCKED (intrinsics parse)

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/scan_server/scan_server  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: build/tools/macos_app/OpenScanner.app/Contents/MacOS/scan_server (07:44:44) — refreshed only on an app build (#107)
- Preflight: pwd OK; no conflicting process; daemon: RPCServer(61563,) running (process proxy: a vendor RPC host process exists); freshness: binary 07:44:44 >= newest source main.cpp 06:06:04
- Interposer: expect line 2: [vulkan_disable_interpose] loaded  (scan_server sets DYLD_INSERT_LIBRARIES itself) — CONFIRMED present on the successful run
- Run start: 2026-10-24 07:45:25

## Preconditions

- Vendor daemon: started deliberately for this card — `launchctl bootstrap gui/501 /Library/LaunchAgents/com.creality.RPCServer.plist` (rc 0; pids 61561/61563) — it was not running beforehand (this card needs it live, unlike the #181/#198 cards, which needed it stopped).
- **Scene match: NOT verified.** The operator confirmed (asked directly) that today's physical scene is not known to match the 2026-10-20 `#132` IR-capture scene. This recording is therefore **not** a valid same-scene pair for the #207 triangulation A/B as specified — it stands only as a fresh, clean vendor-cloud recording and a cross-check of the intrinsics blob's stability. The A/B itself stays open.

## Commands

```
# first attempt — malformed, see "On failure or block" below
build/tools/scan_server/scan_server --live 8787 /tmp/openscanner_live_server_session 30 \
    --record /tmp/claude-501/plan207-vendor-cloud.rec

# retry — corrected argument order ([path] is the vendor SDK library path, not an
# output directory; empty string lets it auto-detect)
build/tools/scan_server/scan_server --live 8787 "" 30 \
    --record /tmp/claude-501/plan207-vendor-cloud.rec
```

## Result

- Exit code: `0` (the corrected retry); the first attempt aborted (signal 6) before any frame flowed — see below
- Verdict: **PASS** for the recording itself; **BLOCKED** for the intrinsics numeric parse (the blob is not the plain layout the recon doc describes)
- Raw log: `2026-10-24-plan207-vendor-cloud-intrinsics.raw.log` (same directory)
- Interposer confirmation: `[vulkan_disable_interpose] loaded` present on the successful retry (not reached on the aborted first attempt)
- Recording: [`2026-10-24-plan207-vendor-cloud.rec`](./2026-10-24-plan207-vendor-cloud.rec) (5,287,676 B, V1 frames)
- Model: [`2026-10-24-plan207-vendor-cloud-model.ply`](./2026-10-24-plan207-vendor-cloud-model.ply) (84,220 B, the vendor's fused model for this session, copied from the session's `result/model.ply`)
- Session dir (ephemeral, `/tmp`, not preserved by this repo): `openscanner_live_server_session/stfu2ah_XcMUM7_vXJy6x_o7aQsA/`

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | RPCServer running before launch (deliberately started) | **[PASS]** pids 61561/61563 after `launchctl bootstrap` |
| 2 | Preflight exits 0 for `scan_server` | **[PASS]** `hw-preflight.sh scan_server: OK` (after a rebuild — stale binary against `main.cpp`) |
| 3 | SDK loads, device found, scan starts | **[PASS]** `obscan_context_create -> OB_SCAN_SUCCESS`, device count 1, `obscan_scan_preview -> OB_SCAN_SUCCESS` |
| 4 | FRAME_READY flowing for the session | **[PASS]** continuous `FRAME_READY tracking_status=0 (SUCCESS)` lines for the full ~30 s |
| 5 | Clean teardown | **[PASS]** `obscan_session_destroy -> OB_SCAN_SUCCESS`, `obscan_context_destroy -> OB_SCAN_SUCCESS`, `[worker] Frame protocol V1 counters: cloud_frames=301 full_detail_chunks=1`, `tracking_status samples=923 histogram: SUCCESS=923`, `[launcher] Worker exited normally with code 0.` |
| 6 | `camera_intrin.bat` (210 B per the recon doc; 209 B observed) is byte-identical across independent sessions (device property, scene-independent) | **[PASS]** identical hex across this fresh session and two leftover sessions from earlier today (03:38, 04:28) |
| 7 | `camera_intrin.bat` parses as the "41-field big-endian record" the recon doc (`calibration-store.md` §4) calls **settled** | **[FAIL]** the raw bytes do not read as plaintext floats/ints at all: `00000000000000DB 0000000000000020 00000000000000B1 AFAFAFAFAFAFAFAF` then ~185 bytes of high-entropy data with no plausible fx/fy/cx/cy pattern at any 4-byte-aligned offset tried |

## Intrinsics blob — raw evidence (no numbers extracted)

`camera_intrin.bat`, 209 bytes, identical across all three sessions checked:

```
00000000000000DB000000000000002000000000000000B1AFAFAFAFAFAFAFAF40A2B9704C4CA07E7EF87E7E7EB47EED358AD01E551231B9911D83CDE2D1A1663C297CF02B8F929CC8B6C928FAABE5DBC9E24D25A5D45A4D7BAF81250900EB8D9454935F02B2ABDF240F98B3224859FD7A8097839596F6B6FB6F730B7AE7DB8C48CC7E7CF850CF8681ED4F20C29BDA7A2A25F221CCF2D5179B1A9B3F20F60A753B1FCD20461615E001212D4B75F9E9CA4B7E071E92C7AE1414735CAEAE7CC08E55AB29A25FA2C030B6161F63FFDE5A5B59
```

The first 24 bytes read as three big-endian u64 values — `0xDB` (219), `0x20`
(32), `0xB1` (177) — which do not obviously correspond to field positions,
offsets, or lengths that sum to 209 in any combination this agent tried
(219+32+177=428; 32+177=209 minus the 24-byte header itself doesn't close
either: 24+177=201, not 209). The next 8 bytes are a repeated `0xAF` byte —
consistent with a fixed header/padding pattern, not data — and everything
after that is high-entropy with no repeating structure. This does not match
`calibration-store.md`'s description ("41 fields... anchored semantics...
read with any SQLite client + §3's map; no getter") as something directly
parseable; the finding's "Intrinsics — settled" claim is evidently about the
*transport* (which file/row holds it, confirmed correct here) and the
*source-struct* offsets inside the live `DeviceParameters` object (which this
agent cannot read — that lives in process memory, not this file), not about
the packed buffer's own byte layout, which the field table itself marks
`[open]` for most entries. No fx/fy/cx/cy numbers are reported here; reporting
a guessed offset as if it were the real values would be worse than reporting
none.

## Crash sweep

Crash report matched by app_name + timestamp: [`2026-10-24-plan207-vendor-cloud-intrinsics.crash.ips`](./2026-10-24-plan207-vendor-cloud-intrinsics.crash.ips), from `scan_server-2026-10-24-074543.ips`, sha256 `41735c3e99800b7039d8743ffdc738e295e5dca7eb4833dd6bffcc91673b3b15` (incident `AFC912BB-A28A-4CBB-A62E-BCCD59C3D018`). This is from the **first, malformed invocation** (below), not the successful recording — the retry with corrected arguments produced no crash and exited 0.

## On failure or block — observable + one lead

- Observable: the first `scan_server --live 8787 /tmp/openscanner_live_server_session 30 --record ...` invocation aborted immediately: `dyld[61811]: terminating because inserted dylib '/tmp/libMoltenVK.dylib' could not be loaded... tried: ... (no such file)` / `[launcher] Worker crashed or timed out inside the vendor SDK (child killed by signal Abort trap: 6).` No frames flowed; a crash report was generated (see Crash sweep).
- Lead: this agent's own argument-order error, not a product defect — `tools/scan_server/main.cpp`'s `RunLiveMode` treats the second positional argument as `library_path` (the vendor SDK dylib's directory, from which `libMoltenVK.dylib`'s path is derived), not an output/session directory as this agent assumed from the usage comment's `[path]` label; passing a non-library directory there silently produces a bogus `libMoltenVK.dylib` path and a dyld abort. The usage comment at the top of `main.cpp` (`scan_server --live [port] [path] [seconds] [--record <file>]`) does not say what `[path]` means, which is exactly how this agent misread it — worth a one-word rename or comment fix for the next reader, but that edit is not this agent's to make.
- Separately, for the intrinsics parse itself: likely the packed `camera_intrin.bat` buffer is not the straightforward sequential layout implied by `calibration-store.md`'s field-order table — the table's own `offset` column is explicitly the *source* offset inside live `DeviceParameters`, not a claim about this file's byte layout, and the observed header (`0xDB`/`0x20`/`0xB1`/`0xAF`-run) plus high-entropy payload suggests either a length-prefixed/checksummed container or encoding this agent should not attempt to reverse — that is recon's or #208's job, not hw-test's.

## Open questions

- Does `calibration-store.md` need a correction/erratum: is the packed `camera_intrin.bat` buffer's byte layout actually unresolved (contradicting its §4 "Intrinsics — settled" framing), given the raw bytes observed here don't match a plain 41-field record?
- What is the actual container format of `camera_intrin.bat` (209 B, stable across sessions) — a checksum/length-prefixed header over an encoded payload, or something else — and can it be decoded without touching vendor binaries?
- Without a verified scene match to `#132`, is this recording still useful to #207's A/B as a *different*-scene sanity check (e.g. frame-rate/format conformance), or does #207 need a fresh, deliberately scene-matched pair before any triangulation comparison is meaningful?

## Board

- Row move: #207 (`BUILD`) — no move; this run supplies one ingredient (a fresh vendor-cloud recording) but **not** a scene-matched pair, so the A/B precondition is still open. #208 (`TODO`) — no move; the intrinsics numbers it needs ("the numbers are still owed") remain owed — this run locates and reproduces the stable raw blob but could not parse fx/fy/cx/cy from it, and flags that the blob's layout may not be as settled as `calibration-store.md` currently states. Suggest filing that as a correction/erratum on `calibration-store.md` for recon, separate from #208's own row.
