#!/usr/bin/env python3
"""Generate a simple OpenScanner .icns without any image library.

Writes the required iconset PNGs (zlib-compressed, no deps) and runs
iconutil over them. See docs/recomp/ROADMAP.md A2.
"""
import os
import struct
import subprocess
import sys
import zlib


def png_bytes(size):
    cx = cy = (size - 1) / 2.0
    radius = size * 0.34
    rows = []
    for y in range(size):
        row = bytearray([0])  # filter byte
        for x in range(size):
            dx, dy = x - cx, y - cy
            inside = (dx * dx + dy * dy) <= radius * radius
            if inside:
                r, g, b = 235, 200, 90
            else:
                r, g, b = 24, 40, 60
            row += bytes((r, g, b, 255))
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)  # RGBA
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "OpenScanner.icns"
    iconset = os.path.splitext(out)[0] + ".iconset"
    os.makedirs(iconset, exist_ok=True)
    mapping = {
        "icon_16x16.png": 16, "icon_16x16@2x.png": 32,
        "icon_32x32.png": 32, "icon_32x32@2x.png": 64,
        "icon_128x128.png": 128, "icon_128x128@2x.png": 256,
        "icon_256x256.png": 256, "icon_256x256@2x.png": 512,
        "icon_512x512.png": 512, "icon_512x512@2x.png": 1024,
    }
    for name, size in mapping.items():
        with open(os.path.join(iconset, name), "wb") as f:
            f.write(png_bytes(size))
    subprocess.check_call(["iconutil", "-c", "icns", iconset, "-o", out])
    print("wrote", out)


if __name__ == "__main__":
    main()
