#!/usr/bin/env python3
"""Offline regression for frame_decode (row #125).

Renders the two committed bands (one depth, one IR) and asserts the exact
output bytes (sha256), the dimensions, the stats and the byte accounting, plus
the `--band-offset` assembly hook. Runs under `ctest -LE hardware`; a regression
fails here, not on a device.
"""

import hashlib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "frame_decode.py")
FIX = os.path.join(HERE, "fixtures")
sys.path.insert(0, HERE)
import frame_decode  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    if not ok:
        print("[FAIL] " + what, file=sys.stderr)
        failures += 1


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def band(name):
    with open(os.path.join(FIX, name), "rb") as f:
        return f.read()


# Recorded at row #125; a changed fixture or renderer must update these.
FIXTURES = {
    "depth_band.bin": "559699c7563cad8d03d3cbc300fb621c17c837e12b4ad1bf59024ed2d016ef40",
    "ir_band.bin": "c25c9f365f78ba6fe085822beb311bf371dd8aec98206b1cbcaf6afdca696b5b",
}
# (bin, rows, partial, length, min, max, mean, ff_share, pgm_sha256)
EXPECTED = {
    "depth_band.bin": (6, 730, 12250, 0, 255, 38.97, 0.0162,
                       "23bf39c66f2e37a41096c2e4fbde2adb2d824ce28b4cc4d6d87b16335166e4a4"),
    "ir_band.bin": (6, 756, 12276, 15, 51, 16.50, 0.0,
                    "5aaf4fe599436e976ae49c9d72c31db58a19d94dacc84f3aec1ecf343dfe9073"),
}


def main():
    for name, want in FIXTURES.items():
        check(sha(os.path.join(FIX, name)) == want, "fixture sha256 " + name)

    with tempfile.TemporaryDirectory() as tmp:
        for name, exp in EXPECTED.items():
            rows, partial, length, vmin, vmax, mean, ff, pgm_sha = exp
            payload = band(name)
            st = frame_decode.band_stats(payload, frame_decode.DEFAULT_STRIDE)
            check(st["rows"] == rows, name + " rows")
            check(st["partial"] == partial, name + " partial")
            check(st["length"] == length, name + " length")
            check(st["length"] == st["pixels"] + st["partial"], name + " byte accounting")
            check(st["min"] == vmin and st["max"] == vmax, name + " value range")
            check(abs(st["mean"] - mean) < 0.01, name + " mean")
            check(abs((st["ff"] / st["length"]) - ff) < 0.0001, name + " 0xFF share")

            out = os.path.join(tmp, name)
            rc = subprocess.run([sys.executable, TOOL, "--bin", os.path.join(FIX, name), "--out", out],
                                capture_output=True, text=True)
            check(rc.returncode == 0, name + " CLI exit: " + rc.stderr.strip())
            pgm = os.path.join(out, os.path.splitext(name)[0] + ".pgm")
            check(sha(pgm) == pgm_sha, name + " PGM sha256")
            with open(pgm, "rb") as f:
                header = f.readline() + f.readline() + f.readline()
            check(header == ("P5\n1920 %d\n255\n" % rows).encode(), name + " PGM header/dims")

        # The assembly hook: --band-offset places the band in a taller canvas;
        # it must not alter the band's own bytes.
        name = "ir_band.bin"
        payload = band(name)
        out = os.path.join(tmp, "offset")
        rc = subprocess.run([sys.executable, TOOL, "--bin", os.path.join(FIX, name),
                             "--out", out, "--band-offset", "6"], capture_output=True, text=True)
        check(rc.returncode == 0, "band-offset exit")
        with open(os.path.join(out, "ir_band.pgm"), "rb") as f:
            canvas = f.read()
        rows = frame_decode.band_stats(payload, 1920)["rows"]
        band_region = payload[: rows * 1920]
        check(canvas.startswith(b"P5\n1920 %d\n255\n" % (rows + 6)), "band-offset height")
        check(canvas.endswith(band_region), "band-offset does not alter the band bytes")
        check(len(canvas) == len(b"P5\n1920 %d\n255\n" % (rows + 6)) + (rows + 6) * 1920, "band-offset size")

        # Deterministic: a second run is byte-identical.
        again = os.path.join(tmp, "again")
        subprocess.run([sys.executable, TOOL, "--bin", os.path.join(FIX, "depth_band.bin"),
                        "--out", again], check=True, capture_output=True)
        check(sha(os.path.join(again, "depth_band.pgm"))
              == sha(os.path.join(tmp, "depth_band.bin", "depth_band.pgm")), "deterministic output")

    if failures:
        print("[frame_decode_test] %d check(s) failed" % failures, file=sys.stderr)
        return 1
    print("[frame_decode_test] PASS: depth+ir bands render to the recorded PGMs, dims/byte accounting/"
          "stats exact, band-offset hook preserves the band, output deterministic")
    return 0


if __name__ == "__main__":
    sys.exit(main())
