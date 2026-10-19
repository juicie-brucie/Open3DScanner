- 2026-10-19 — **#124's plan landed** (`docs/recon/findings/frame-codec.md`): step 0 (settle the
  result's msgpack shape) + T1 magic scan, T2 standard-decoder trials (MJPEG first), T3 entropy,
  T4 size-vs-content, T5 header sharing, T6 self-describing structure scan — per interface (IR
  10013, depth 10010); a five-hypothesis table with an explicit falsifier each; and the
  acceptance line. **The same pass corrected the plan's own pre-flight decode**, which was a
  msgpack mis-reading: the recorded 24-byte frame head decodes **completely** (24 of 24 bytes, no
  residue) as `[1, 5, nil, [0, 0, 0, 255, b'', 0, b'', 0, b'', 2731083288, -1]]`, and the
  daemon's segment is `tcp.len = 24` — so the repo holds a **whole** message, not a prefix. The
  result is an **11-element array** (element 9 is a complete uint32, `ce a2 c9 06 18` =
  2,731,083,288 — nothing is cut), and `0xcf` is **uint64**, not int8. The operative conclusions
  stand: the spec's `bin8(52,992)` gloss is invalid, the result is an array, and the array's field
  layout (which slot carries the payload) is unsettled. The live #120+#122 replay rerun
  corroborates the array: every live `get_frame` reply's unwrapped result is `kind=7` (Array),
  never `kind=6` (Bin). **Reported to the board, not changed here:** `tools/frame_bins` classifies
  a response only when the envelope's index 3 is a **top-level** bin, so an array-shaped
  `get_frame` result yields nothing — #128's multi-segment reassembly fix is necessary but not
  sufficient; the classifier must also descend into the result array. Row #124 stays `BLOCKED`
  (the corpus waits on #128).
