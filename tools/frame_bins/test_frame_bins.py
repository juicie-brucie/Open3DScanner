#!/usr/bin/env python3
"""CI test for frame_bins (row #123): extract the synthetic fixture and assert
the counts, port split, order, bytes and index; re-run to prove determinism."""

import hashlib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "frame_bins.py")
FIXTURE = os.path.join(HERE, "fixtures", "synthetic_capture.tsv")

failures = 0


def check(ok, what):
    global failures
    if not ok:
        print("[FAIL] " + what, file=sys.stderr)
        failures += 1


def tree_digest(root):
    h = hashlib.sha256()
    for dirpath, _dirs, files in os.walk(root):
        for name in sorted(files):
            path = os.path.join(dirpath, name)
            h.update(os.path.relpath(path, root).encode() + b"\0")
            with open(path, "rb") as f:
                h.update(f.read())
    return h.hexdigest()


def extract(out):
    return subprocess.run([sys.executable, TOOL, "--input", FIXTURE, "--out", out],
                          capture_output=True, text=True)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        a = os.path.join(tmp, "a")
        rc = extract(a)
        check(rc.returncode == 0, "tool exit: " + rc.stderr.strip())

        check(sorted(os.listdir(os.path.join(a, "depth"))) == ["000001.bin"], "depth files")
        check(sorted(os.listdir(os.path.join(a, "ir"))) == ["000001.bin", "000002.bin"], "ir files")

        with open(os.path.join(a, "depth", "000001.bin"), "rb") as f:
            check(f.read() == b"\x01\x02\x03\x04", "depth payload bytes")
        with open(os.path.join(a, "ir", "000001.bin"), "rb") as f:
            check(f.read() == b"\xaa\xbb\xcc\xdd\xee", "ir seq 1 bytes")
        with open(os.path.join(a, "ir", "000002.bin"), "rb") as f:
            check(f.read() == b"\x21\x22\x23", "ir seq 2 bytes")

        with open(os.path.join(a, "index.tsv"), encoding="utf-8") as f:
            lines = f.read().splitlines()
        check(lines[0] == "port\tsequence\ttimestamp\tbyte_length\tmethod\tsource\tfile", "index header")
        check(len(lines) == 4, "index rows = 3")
        cols = [line.split("\t") for line in lines[1:]]
        check([c[0] for c in cols] == ["depth", "ir", "ir"], "index port order")
        check([c[1] for c in cols] == ["1", "1", "2"], "index sequence order")
        check([c[3] for c in cols] == ["4", "5", "3"], "index byte lengths")
        check(all(c[4].startswith("ob_rpc_get_frame") for c in cols), "only frame methods")
        check([c[2] for c in cols] == ["100.001000000", "100.005000000", "100.007000000"], "timestamps")

        # Determinism: the same input produces the same tree.
        b = os.path.join(tmp, "b")
        rc2 = extract(b)
        check(rc2.returncode == 0, "second run exit")
        check(tree_digest(a) == tree_digest(b), "same input -> identical output")

        # The decoys are counted but not written as frame bins.
        check("responses=2" in rc.stdout and "written=1" in rc.stdout, "depth response/written counts")
        check("responses=3" in rc.stdout and "written=2" in rc.stdout, "ir response/written counts")

    if failures:
        print("[frame_bins_test] %d check(s) failed" % failures, file=sys.stderr)
        return 1
    print("[frame_bins_test] PASS: 1 depth + 2 ir frame bins, order/bytes/index/determinism OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
