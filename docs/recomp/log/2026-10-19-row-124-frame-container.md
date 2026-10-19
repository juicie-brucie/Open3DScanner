- 2026-10-19 — recon (recon room, `wt-recon5`, branch `recon/row124-frame-codec`): **#124's
  frame container is named — it is the RPC envelope, and the payload is raw pixels, not a codec.**

  **The corpus, and its size.** The fixed extractor
  (`tools/frame_bins/frame_bins.py` from `build/plan128-multisegment` @ `6e13eb2` — **not** on
  `origin/main`, whose copy has no `iter_bins` and no `94 00`/`94 01` scan) was run from a temp
  path over `session4.pcapng`. It wrote **33 bins from 11 replies**: 10 depth + 1 IR, each reply
  contributing three bins (slot 4 = the frame payload, slots 6/8 = companions). **n = 11** — and
  the IR interface is **n = 1**. Every number in the finding is over that n. 5,875 of the 5,886
  `get_frame`-shaped replies in the capture carry no payload at all.

  **The container.** `ob_rpc_get_frame` answers `[1, msgid, nil, result]` with `result` = an
  **11-element fixarray**; the frame payload is **element 4**, a `bin16`. The layout is byte-exact
  (all 11 elements, widths, endianness) and holds on 11/11 with zero declared-length exceptions.
  **The payload has no header of its own**: no standard magic (0/11 over 18 signatures), no
  standard decoder (0/11 over 12 decoders plus `sips`/`ffmpeg`/`zstd`/`lz4`), no per-frame header
  (LCP 0 over 9 consecutive pairs), no self-describing field (0/11 for 1920/1200/50/9 and for a
  self-length).

  **The payload is raw 8-bit image samples.** The normalised autocorrelation's strongest lag is
  **1920** in 10/10 depth frames and 1/1 IR frame; 12.6–67.9 % of adjacent bytes are equal; adjacent
  rows are near-identical; the 0xFF invalid bands recur every 1920 B. Entropy is 2.96–7.22
  bits/byte (IR 0.76) — not compressed, not obfuscated. **H4 survives; H1, H3 and H5 are falsified.**

  **The payload is a band, not the frame.** 12,118–12,276 B = 6 rows + a partial row of a
  1920-wide image (x188.5 against 1920x1200). Over **5,886** replies the **maximum declared
  payload length is 12,276 B** — the daemon never declares 2,304,000 B, so it returns the image in
  ~12 KB bands, one per reply, and the client stitches them. `get_frame`'s request carries empty
  params, so the split is the daemon's.

  **Byte accounting closes exactly on 11/11** (envelope + the 11 elements = the measured message
  length, zero residue).

  **Acceptance: 2 clauses met, 1 partial, 1 met — not met in full, so the row is `SPEC-READY`, not
  `DONE`.** Clause (2) cannot be met by a container that carries **no** width/height/format field:
  the width 1920 is corroborated by the payload's row stride and the format is 8-bit single-channel
  (Y8), but the **height 1200 is not in the container** — it lives in `start_stream`'s params. The
  band-assembly rule is also open, and the IR numbers rest on a single frame.

  **Next evidence, named:** -> **#131** REA the producer (`/Library/Creality/RPCServer` +
  `libOrbbecSDK.1.7.1.dylib`) for the frame serializer — it settles the band rule, `e2`/`e6`/`e8`
  and the two reply forms; -> **#132** a gap-free capture of the running stream (>= 100 contiguous
  `get_frame` replies) to test whether band *k+1* continues band *k* and to recover the height; the
  pixel scale comes from #126's controlled scene.

  **Corrections to the repo's records.** `protocol-spec.md` §1.3's "`[1, msgid, nil, bin(N)]`" is
  **wrong** — the result is an 11-array and the payload is element 4 — and its "10–53 KB" upper
  bound is **not corroborated** (max 12,276 B anywhere in the capture). The old §1.2's recorded
  24-byte head is the **no-data** form (all three buffers empty): it never carried a frame, so it
  could never have shown the payload's first bytes. #127's question ("what is inside the result
  array") is answered capture-side; what remains for it is the client-side live dump.
