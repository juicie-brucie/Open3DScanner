# frame_decode — a band renderer, not a decoder (row #125)

The container is settled (`docs/recon/findings/frame-codec.md`): an
`ob_rpc_get_frame` reply is a msgpack-RPC envelope whose **element 4** is a
`bin16` of **headerless raw 8-bit pixels** with a **1920-byte row stride**. A
reply carries a **band** (~6 rows + a partial row), not a whole frame.

This tool reinterprets a band as a 1920-wide image, writes an 8-bit **PGM**, and
reports stats. For **depth** it writes the PGM and stats **only** — no metric
value, no PLY: the value→depth scale is not known (#132), and a PLY with
invented units would be a false claim.

```sh
frame_decode.py --index <frame_bins output> [--slot 4] --out DIR
frame_decode.py --bin <band.bin> --out DIR [--band-offset ROW]
```

`--slot` defaults to `4` (the payload). Slots `6`/`8` are **companions, not
frames** (`frame-codec.md` §2.3); `--slot all` renders them only for inspection.

## Output

`<out>/<port>_<sequence>_slot<slot>.pgm` — `P5`, width 1920, height = full rows
(`len / 1920`). A partial final row is **reported, never padded or dropped**.

Stats per band: rows × 1920, partial bytes, value min/max/mean, the `0xFF`
("invalid") share, and the byte accounting (`len == rows*1920 + partial`).

## The assembly hook

The full-frame assembly rule is **open** (#131/#132). `--band-offset ROW` places
the band at that row of a taller, zero-filled canvas so a future caller can
stack bands — the tool does **not** guess the rule or the band ordering.

## Fixtures and the offline regression

Committed under `fixtures/` (~24 KB total), from the #123 extraction of
`session4.pcapng`:

| fixture | sha256 |
| --- | --- |
| `depth_band.bin` (12,250 B) | `559699c7563cad8d03d3cbc300fb621c17c837e12b4ad1bf59024ed2d016ef40` |
| `ir_band.bin` (12,276 B) | `c25c9f365f78ba6fe085822beb311bf371dd8aec98206b1cbcaf6afdca696b5b` |

Rendered PGM sha256s (asserted by `test_frame_decode.py`):

| output | sha256 |
| --- | --- |
| `depth_band.pgm` | `23bf39c66f2e37a41096c2e4fbde2adb2d824ce28b4cc4d6d87b16335166e4a4` |
| `ir_band.pgm` | `5aaf4fe599436e976ae49c9d72c31db58a19d94dacc84f3aec1ecf343dfe9073` |

The test also asserts the dimensions, value range, `0xFF` share, byte
accounting, the `--band-offset` hook, and determinism. It runs under
`ctest -LE hardware`.
