#!/usr/bin/env python3
"""frame_decode — render a captured frame band (row #125).

The container is settled (findings frame-codec.md): `ob_rpc_get_frame` answers
with a msgpack-RPC reply whose element 4 is a `bin16` carrying **headerless raw
8-bit pixels** at a **1920-byte row stride**. Each reply is a **band** (a few
rows plus a partial row), not a whole frame.

This tool is a *renderer*, not a decoder: it reinterprets the bytes as a
1920-wide image, writes an 8-bit PGM, and reports the stats (dimensions, value
range, 0xFF share, byte accounting). For depth it writes the PGM and stats
**only** — no metric value and no PLY: the value→depth scale is not known
(#132), and a PLY with invented units would be a false claim.

Input:
    --index DIR [--slot 4]      every matching row of a frame_bins index.tsv
    --bin FILE --slot N         one extracted bin

The full-frame assembly rule is OPEN (#131/#132). `--band-offset ROW` is an
*assembly hook*: it places the band at that row of a taller canvas so a future
caller can stack bands, but this tool does not guess the rule.

Exit codes: 0 = rendered, 1 = a parse/render failure, 2 = bad arguments.
"""

import argparse
import hashlib
import os
import sys

DEFAULT_STRIDE = 1920


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def band_stats(payload, stride):
    """Dimensions and value statistics for one band."""
    rows, partial = divmod(len(payload), stride)
    count = len(payload)
    total = sum(payload)
    return {
        "width": stride,
        "rows": rows,
        "partial": partial,
        "length": count,
        "pixels": rows * stride,
        "min": min(payload) if payload else 0,
        "max": max(payload) if payload else 0,
        "mean": (total / count) if count else 0.0,
        "ff": payload.count(0xFF),
    }


def render_pgm(payload, stride, band_offset=0):
    """PGM bytes for the full rows, optionally placed at `band_offset`."""
    rows, _partial = divmod(len(payload), stride)
    full = payload[: rows * stride]
    height = rows + band_offset
    header = ("P5\n%d %d\n255\n" % (stride, height)).encode("ascii")
    if band_offset:
        return header + (b"\x00" * (band_offset * stride)) + full
    return header + full


def read_index(path):
    """Yield (port, sequence, byte_length, slot, file) from a frame_bins index."""
    index_path = os.path.join(path, "index.tsv")
    with open(index_path, "r", encoding="utf-8") as f:
        header = f.readline().rstrip("\n").split("\t")
        if "slot" not in header:
            raise SystemExit("frame_decode: %s has no slot column (old frame_bins output)" % index_path)
        idx = {name: i for i, name in enumerate(header)}
        for line in f:
            cols = line.rstrip("\n").split("\t")
            if len(cols) != len(header):
                continue
            yield (cols[idx["port"]], cols[idx["sequence"]], cols[idx["slot"]],
                   cols[idx["file"]], os.path.join(path, cols[idx["port"]], cols[idx["file"]]))


def process(name, payload, out_dir, stride, band_offset, write_image=True):
    st = band_stats(payload, stride)
    record = {
        "name": name,
        "width": st["width"],
        "rows": st["rows"],
        "partial": st["partial"],
        "length": st["length"],
        "pixels": st["pixels"],
        "min": st["min"],
        "max": st["max"],
        "mean": st["mean"],
        "ff_share": (st["ff"] / st["length"]) if st["length"] else 0.0,
        "pgm": "",
        "pgm_sha256": "",
    }
    if write_image and st["rows"] > 0:
        pgm = render_pgm(payload, stride, band_offset)
        os.makedirs(out_dir, exist_ok=True)
        pgm_path = os.path.join(out_dir, name + ".pgm")
        with open(pgm_path, "wb") as f:
            f.write(pgm)
        record["pgm"] = pgm_path
        record["pgm_sha256"] = sha256(pgm)
    return record


def print_header():
    print("name                                   rows  partial  len     rows*1920  min  max  mean   0xFF%   bytes-close")


def print_record(r):
    print("%-38s %5d %8d %7d %9d %4d %4d %6.2f %6.2f%%  %s"
          % (r["name"], r["rows"], r["partial"], r["length"], r["pixels"], r["min"], r["max"],
             r["mean"], r["ff_share"] * 100.0,
             "yes" if r["length"] == r["pixels"] + r["partial"] else "NO"))


def main(argv):
    ap = argparse.ArgumentParser(description="render a captured raw frame band (row #125)")
    ap.add_argument("--index", help="a frame_bins output directory (index.tsv + <port>/)")
    ap.add_argument("--bin", help="one extracted bin file")
    ap.add_argument("--slot", default="4",
                    help="slot to render from --index (default 4, the payload; 'all' = every slot)")
    ap.add_argument("--out", required=True, help="output directory for the PGM images")
    ap.add_argument("--stride", type=int, default=DEFAULT_STRIDE, help="row stride (default 1920)")
    ap.add_argument("--band-offset", type=int, default=0,
                    help="place the band at this row of a taller canvas (assembly hook; the rule is open)")
    ap.add_argument("--stats-only", action="store_true", help="print stats, write no images")
    args = ap.parse_args(argv)

    if bool(args.index) == bool(args.bin):
        ap.error("pass exactly one of --index or --bin")
    if args.stride <= 0:
        ap.error("--stride must be positive")
    if args.band_offset < 0:
        ap.error("--band-offset must be >= 0")

    records = []
    if args.bin:
        with open(args.bin, "rb") as f:
            payload = f.read()
        name = os.path.splitext(os.path.basename(args.bin))[0]
        records.append(process(name, payload, args.out, args.stride, args.band_offset,
                               write_image=not args.stats_only))
    else:
        for port, sequence, slot, filename, path in read_index(args.index):
            if args.slot != "all" and slot != args.slot:
                continue
            with open(path, "rb") as f:
                payload = f.read()
            name = "%s_%06d_slot%s" % (port, int(sequence), slot)
            records.append(process(name, payload, args.out, args.stride, args.band_offset,
                                   write_image=not args.stats_only))

    if not records:
        print("frame_decode: no bins matched (slot=%s)" % args.slot, file=sys.stderr)
        return 1
    print_header()
    for r in records:
        print_record(r)
    print("frame_decode: %d band(s); assembly rule is OPEN — --band-offset is a hook, not a rule"
          % len(records))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
