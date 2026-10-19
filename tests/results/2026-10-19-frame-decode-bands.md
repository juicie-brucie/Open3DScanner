# PLAN #125 — band renderer: the 11 extracted frames

**Date**: 2026-10-19
**Target**: `tools/frame_decode/frame_decode.py` (offline; no device)
**Input**: the #123/#128 extraction of `session4.pcapng` — the 11 slot-4
`get_frame` payloads (10 depth + 1 IR).
**Command**: `frame_decode.py --index /tmp/fb-a --slot 4 --out /tmp/fd_real`
**Exit**: 0

No vendor binary was read; the container is the spec in
`docs/recon/findings/frame-codec.md`: element 4, headerless raw 8-bit pixels,
1920-byte row stride, a band rather than a whole frame.

## Stats table

| band | rows | partial | len | rows×1920 | min | max | mean | 0xFF | bytes close |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| depth_000001_slot4 | 6 | 730 | 12,250 | 11,520 | 0 | 255 | 38.97 | 1.62 % | yes |
| depth_000004_slot4 | 6 | 737 | 12,257 | 11,520 | 0 | 255 | 172.63 | 49.96 % | yes |
| depth_000007_slot4 | 6 | 730 | 12,250 | 11,520 | 0 | 255 | 179.92 | 48.40 % | yes |
| depth_000010_slot4 | 6 | 723 | 12,243 | 11,520 | 0 | 255 | 199.62 | 65.53 % | yes |
| depth_000013_slot4 | 6 | 724 | 12,244 | 11,520 | 0 | 255 | 192.08 | 60.13 % | yes |
| depth_000016_slot4 | 6 | 720 | 12,240 | 11,520 | 0 | 255 | 190.40 | 61.48 % | yes |
| depth_000019_slot4 | 6 | 714 | 12,234 | 11,520 | 0 | 255 | 189.18 | 58.66 % | yes |
| depth_000022_slot4 | 6 | 721 | 12,241 | 11,520 | 0 | 255 | 190.11 | 57.94 % | yes |
| depth_000025_slot4 | 6 | 601 | 12,121 | 11,520 | 0 | 255 | 186.24 | 59.05 % | yes |
| depth_000028_slot4 | 6 | 598 | 12,118 | 11,520 | 0 | 255 | 144.88 | 8.01 % | yes |
| ir_000001_slot4 | 6 | 756 | 12,276 | 11,520 | 15 | 51 | 16.50 | 0.00 % | yes |

Every band is **6 full rows plus a partial row** (598–756 B), so a reply carries
~6.4 rows of the 1920-wide image — consistent with the "band, not a frame"
finding. The byte accounting closes exactly in 11/11
(`len == rows*1920 + partial`).

The depth bands are unscaled 8-bit bytes: min/max/mean are reported, but **no
metric value is claimed** and no PLY is written (the value→depth scale is
#132's question). IR values span 15–51 with no `0xFF`, consistent with the IR
payload's narrow band noted in the findings.

## Offline regression

`tools/frame_decode/test_frame_decode.py` (ctest `frame_decode_test`) asserts
the fixture sha256s, the rendered PGM sha256s, the dimensions, value range,
`0xFF` share, byte accounting, the `--band-offset` hook, and determinism.

Fixtures (from the same extraction):

| fixture | sha256 |
| --- | --- |
| `depth_band.bin` (12,250 B) | `559699c7563cad8d03d3cbc300fb621c17c837e12b4ad1bf59024ed2d016ef40` |
| `ir_band.bin` (12,276 B) | `c25c9f365f78ba6fe085822beb311bf371dd8aec98206b1cbcaf6afdca696b5b` |

Rendered PGM sha256s:

| output | sha256 |
| --- | --- |
| `depth_band.pgm` | `23bf39c66f2e37a41096c2e4fbde2adb2d824ce28b4cc4d6d87b16335166e4a4` |
| `ir_band.pgm` | `5aaf4fe599436e976ae49c9d72c31db58a19d94dacc84f3aec1ecf343dfe9073` |

**Result**: `ctest -R frame_decode_test` — PASS (also under `-LE hardware`).

## What this does not settle

The **full-frame assembly rule is open** (#131/#132): the band offset within the
frame, the band ordering, and how a partial row joins the next band are not
guessed here. `--band-offset` is a hook for a future caller, not a rule.

Raw output: `2026-10-19-frame-decode-bands.log`.
