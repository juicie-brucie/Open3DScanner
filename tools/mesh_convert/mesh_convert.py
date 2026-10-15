#!/usr/bin/env python3
"""Mesh format conversion CLI for OpenScanner, built on meshio (MIT licensed).

Converts between the mesh formats a scanning pipeline actually needs to move
between: STL (print-ready, no color), OBJ/PLY (textured/vertex-colored scan
output), glTF/GLB, VTK/VTU, and anything else meshio understands.

Usage:
    mesh_convert.py INPUT OUTPUT
    mesh_convert.py INPUT OUTPUT --binary        # force binary output (STL/PLY)
    mesh_convert.py INPUT OUTPUT --ascii         # force ASCII output
    mesh_convert.py --list-formats

Exit codes: 0 on success, 1 on a usage or conversion error.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    import meshio
except ImportError:  # pragma: no cover - dependency hint, not a code path
    print(
        "error: meshio is not installed. Install it with:\n"
        "    pip install -r tools/mesh_convert/requirements.txt",
        file=sys.stderr,
    )
    sys.exit(1)


def list_formats() -> None:
    print("Supported extensions (via meshio):")
    for ext, fmt in sorted(meshio.extension_to_filetypes.items()):
        print(f"  {ext:<10} {', '.join(fmt) if isinstance(fmt, (list, tuple)) else fmt}")


def convert(input_path: Path, output_path: Path, binary: bool | None) -> None:
    if not input_path.exists():
        raise FileNotFoundError(f"input file not found: {input_path}")

    mesh = meshio.read(input_path)

    write_kwargs = {}
    if binary is not None:
        # Not every writer accepts `binary=`; meshio raises TypeError for those
        # that don't, so only pass it through when the writer supports it.
        write_kwargs["binary"] = binary

    try:
        meshio.write(output_path, mesh, **write_kwargs)
    except TypeError:
        meshio.write(output_path, mesh)

    print(
        f"{input_path.name} -> {output_path.name}  "
        f"({len(mesh.points)} points, "
        f"{sum(len(c.data) for c in mesh.cells)} cells)"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="mesh_convert",
        description="Convert a scanned mesh between formats (STL/OBJ/PLY/glTF/VTK/...).",
    )
    parser.add_argument("input", nargs="?", type=Path, help="input mesh file")
    parser.add_argument("output", nargs="?", type=Path, help="output mesh file")
    parser.add_argument(
        "--list-formats", action="store_true", help="list supported formats and exit"
    )
    format_group = parser.add_mutually_exclusive_group()
    format_group.add_argument(
        "--binary", action="store_true", help="write binary output where supported"
    )
    format_group.add_argument(
        "--ascii", action="store_true", help="write ASCII output where supported"
    )

    args = parser.parse_args(argv)

    if args.list_formats:
        list_formats()
        return 0

    if not args.input or not args.output:
        parser.print_usage(sys.stderr)
        return 1

    binary = True if args.binary else (False if args.ascii else None)

    try:
        convert(args.input, args.output, binary)
    except Exception as exc:  # surfaced to the user, not swallowed
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
