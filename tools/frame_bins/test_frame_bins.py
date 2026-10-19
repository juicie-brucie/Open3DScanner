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
PCAP = os.path.join(HERE, "fixtures", "synthetic_capture.pcapng")

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


def extract(out, fmt="tsv", source=None):
    cmd = [sys.executable, TOOL, "--input", source or (PCAP if fmt == "pcap" else FIXTURE),
           "--out", out]
    if fmt == "pcap":
        cmd += ["--format", "pcap"]
    return subprocess.run(cmd, capture_output=True, text=True)


def check_pcap_multisegment():
    """A message spanning >=3 TCP segments is reassembled; a gapped one is dropped."""
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "p")
        rc = extract(out, fmt="pcap")
        check(rc.returncode == 0, "pcap run exit: " + rc.stderr.strip())
        # depth: one frame, 300 B, split across three contiguous segments
        depth = sorted(os.listdir(os.path.join(out, "depth")))
        check(depth == ["000001.bin"], "multi-segment depth bin written")
        with open(os.path.join(out, "depth", depth[0]), "rb") as f:
            check(f.read() == b"\xde" * 300, "3-segment message reassembled byte-for-byte")
        # ir: the contiguous 2-segment frame only; the gapped one must be dropped
        ir = sorted(os.listdir(os.path.join(out, "ir")))
        check(ir == ["000001.bin"], "gapped message dropped (ir has one bin)")
        with open(os.path.join(out, "ir", ir[0]), "rb") as f:
            check(f.read() == b"\xcd" * 120, "contiguous 2-segment ir message")
        with open(os.path.join(out, "index.tsv"), encoding="utf-8") as f:
            rows = [l.split("\t") for l in f.read().splitlines()[1:]]
        check([r[0] for r in rows] == ["depth", "ir"], "pcap index ports")
        check([r[3] for r in rows] == ["300", "120"], "pcap index byte lengths")
        check([r[7] for r in rows] == ["4", "."], "pcap slots: nested depth frame, flat ir bin")


def main():
    with tempfile.TemporaryDirectory() as tmp:
        a = os.path.join(tmp, "a")
        rc = extract(a)
        check(rc.returncode == 0, "tool exit: " + rc.stderr.strip())

        check(sorted(os.listdir(os.path.join(a, "depth"))) == ["000001.bin"], "depth files")
        check(sorted(os.listdir(os.path.join(a, "ir")))
              == ["000001.bin", "000002.bin", "000003.bin", "000004.bin"], "ir files")

        def payload(path):
            with open(path, "rb") as f:
                return f.read()

        check(payload(os.path.join(a, "depth", "000001.bin")) == b"\x01\x02\x03\x04",
              "top-level bin bytes")
        check(payload(os.path.join(a, "ir", "000001.bin")) == b"\xaa\xbb\xcc\xdd\xee", "ir seq 1")
        check(payload(os.path.join(a, "ir", "000002.bin")) == b"\x21\x22\x23", "ir seq 2")
        # the array-shaped reply: its two non-empty bins, slots 4 and 6
        check(payload(os.path.join(a, "ir", "000003.bin")) == b"\xde\xad\xbe\xef\x01", "nested slot 4")
        check(payload(os.path.join(a, "ir", "000004.bin")) == b"\xca\xfe\xba", "nested slot 6")

        with open(os.path.join(a, "index.tsv"), encoding="utf-8") as f:
            lines = f.read().splitlines()
        check(lines[0] == "port\tsequence\ttimestamp\tbyte_length\tmethod\tsource\tfile\tslot",
              "index header carries slot")
        check(len(lines) == 6, "index rows = 5")
        cols = [line.split("\t") for line in lines[1:]]
        check([c[0] for c in cols] == ["depth", "ir", "ir", "ir", "ir"], "index port order")
        check([c[1] for c in cols] == ["1", "1", "2", "3", "4"], "index sequence order")
        check([c[2] for c in cols] == ["100.001000000", "100.007000000", "100.009000000",
                                       "100.013000000", "100.013000000"], "timestamps")
        check([c[3] for c in cols] == ["4", "5", "3", "5", "3"], "index byte lengths")
        check([c[7] for c in cols] == [".", ".", ".", "4", "6"], "slot column values")
        check(all(c[4].startswith("ob_rpc_get_frame") for c in cols), "only frame methods")

        # Determinism: the same input produces the same tree.
        b = os.path.join(tmp, "b")
        rc2 = extract(b)
        check(rc2.returncode == 0, "second run exit")
        check(tree_digest(a) == tree_digest(b), "same input -> identical output")

        # The decoys are counted but not written as frame bins.
        check("responses=3" in rc.stdout and "written=1" in rc.stdout, "depth response/written counts")
        check("responses=4" in rc.stdout and "written=4" in rc.stdout, "ir response/written counts")

    check_pcap_multisegment()

    if failures:
        print("[frame_bins_test] %d check(s) failed" % failures, file=sys.stderr)
        return 1
    print("[frame_bins_test] PASS: tsv top-level + array-shaped (slots 4/6) bins, order/bytes/"
          "index/determinism; pcap 3-segment nested reassembly + gapped message dropped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
