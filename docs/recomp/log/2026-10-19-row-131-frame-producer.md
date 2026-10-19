- 2026-10-19 — recon (recon room, `wt-recon5`, branch `recon/row131-frame-producer`): **#131 — the producer is REA'd; the 11-element
  array is fully named, and the band rule is *not* in the serializer.**

  **The premise needed one correction.** `/Library/Creality/RPCServer` (230 KB) is **not** the daemon that answers `ob_rpc_get_frame`:
  it holds **zero** `ob_rpc_*` strings and exports six symbols. The msgpack-RPC server is `libobsensor::pal::RPCServer` **inside
  `/Library/Creality/libOrbbecSDK.1.7.1.dylib`** (sha256 `2cf0fceb…`), analysed on its arm64 slice `5134a211…` (the already-cached
  `lipo -thin` slice).

  **Settled** — [`findings/frame-codec.md`](../recon/findings/frame-codec.md) §10: the result array is a fixed
  `define_array<u64,u64,u64,OBFormat,vector<char>,uint,vector<char>,uint,vector<char>,uint,OBIRFrameLightType_t>` — **11 elements**, the
  same tuple the SDK's own client unpacks — and **every element is now named**: `e1` = the frame's µs timestamp, `e2` = its second 64-bit
  time word, `e3` = the frame's **format** field (always 255 — not the profile's format id 9), `e4`/`e5` = the frame's **primary data
  buffer** + its length, `e6`/`e7` = its **second** data plane, `e8`/`e9` = its **third** data plane (6 B). **OQ-124-3 is answered**: `e6`
  and `e8` are sibling buffers of the *same* platform frame, not frames and not "companions". The **"two reply forms" are one form** —
  the no-data form is the same array with empty buffers and `e10 = -1` (the producer writes `0xFFFFFFFF` into the high half of the last
  word, which msgpack packs as `int8 -1`), and it **never initialises `e9`**, so `e9` there is uninitialised stack and must be ignored.
  `ob_rpc_get_frame` takes **zero** arguments, and the wire method name is `"ob_rpc_get_frame" + SourcePort-prefix + port-ordinal`, which
  reproduces `ob_rpc_get_frame0-2-10` / `…0-2-12` exactly.

  **The negative result that matters.** `RPCServer::frameCallback` copies the platform frame's three buffers **whole** into a 128-byte
  record on a **256-entry ring buffer**, and `ob_rpc_get_frame` pops **one whole record per call** — the serializer **never splits**. The
  SDK's own `RPCClientPort` **never stitches** either: it hands the received bin to the application unchanged. ⇒ **the band rule is not
  in the SDK's wire layer at all**; it lives in the platform frame producer (§10.7's named reads) or in the CrealityScan app.

  **Row → `SPEC-READY`, not `DONE`** — the band rule itself is still unnamed, so OQ-131-1…4 are carried by **#132**, whose capture
  requirements were sharpened in the same pass (what to capture: the whole 11-element reply per band, both ports, gap-free, ≥ 100
  consecutive replies; what to look at: `e1` monotonicity → the 0xFF phase shift → the 1920-multiple total → `e3` → `e2` → the sibling
  planes → the pixel scale).
