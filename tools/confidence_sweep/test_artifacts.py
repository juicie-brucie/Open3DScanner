#!/usr/bin/env python3
"""The harness's artefact test (row #159).

Runs the tool twice on the synthetic scene and checks that the CSV and the
SVG it writes are byte-identical between the runs (the harness is
deterministic), that the CSV carries both curves, that the tool's exit code
is 3 when the confidence curve is not required to beat random and 0 when it
is, and that the numbers still match the committed synthetic run (the
regression anchor for the estimator and the sweep).

Device-free; runs under `ctest -LE hardware`.
"""

import csv
import hashlib
import os
import subprocess
import sys

FIXTURES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")
# The committed run is compared with a tolerance, not byte for byte: the last
# bits of a float can move between compilers (fused multiply-add contraction),
# and the curve must not depend on that.
RELATIVE_TOLERANCE = 1e-6


def run(tool, args):
    return subprocess.run([tool] + args, capture_output=True, text=True)


def read_rows(path):
    """The data rows of a sweep CSV: the comment lines and the header go."""
    with open(path, newline="") as handle:
        rows = [r for r in csv.reader(handle) if r and not r[0].startswith("#")]
    return rows[1:] if rows else []


def read_header(path):
    with open(path, newline="") as handle:
        for row in csv.reader(handle):
            if row and not row[0].startswith("#"):
                return row
    return []


def compare_to_fixture(path):
    produced = read_rows(path)
    expected = read_rows(os.path.join(FIXTURES, "synthetic_sweep.csv"))
    if len(produced) != len(expected):
        print("the CSV has", len(produced), "rows, the fixture has", len(expected))
        return False
    for a, b in zip(produced, expected):
        if len(a) != len(b):
            print("a row's column count differs:", a, b)
            return False
        for x, y in zip(a, b):
            fx, fy = float(x), float(y)
            if fx == fy:
                continue
            if abs(fx - fy) > RELATIVE_TOLERANCE * max(abs(fx), abs(fy)):
                print("the CSV drifted from the fixture:", x, y)
                return False
    return True


def main(tool):
    out_csv = "artifacts_sweep.csv"
    out_plot = "artifacts_sweep.svg"
    for path in (out_csv, out_plot):
        if os.path.exists(path):
            os.remove(path)

    first = run(tool, ["--synthetic", "--csv", out_csv, "--plot", out_plot, "--quiet"])
    if first.returncode != 0:
        print("the first run failed:", first.returncode, first.stderr)
        return 1
    for path in (out_csv, out_plot):
        if not os.path.exists(path):
            print("the run did not write", path)
            return 1

    csv_bytes = open(out_csv, "rb").read()
    plot_bytes = open(out_plot, "rb").read()
    if not csv_bytes or not plot_bytes:
        print("an artefact is empty")
        return 1
    if not plot_bytes.startswith(b"<?xml") or b"<svg" not in plot_bytes:
        print("the plot is not an SVG")
        return 1

    # The same input must produce the same artefacts.
    second = run(tool, ["--synthetic", "--csv", out_csv, "--plot", out_plot, "--quiet"])
    if second.returncode != 0:
        print("the second run failed:", second.returncode, second.stderr)
        return 1
    if open(out_csv, "rb").read() != csv_bytes:
        print("the CSV is not reproducible")
        return 1
    if open(out_plot, "rb").read() != plot_bytes:
        print("the plot is not reproducible")
        return 1

    # The CSV carries both curves.
    header = read_header(out_csv)
    rows = read_rows(out_csv)
    if not rows:
        print("the CSV has no rows")
        return 1
    for column in ("threshold", "retained_fraction", "rmse", "p95", "rmse_random_mean"):
        if column not in header:
            print("the CSV has no", column, "column:", header)
            return 1
    index = {name: i for i, name in enumerate(header)}
    if len(rows) < 5:
        print("the CSV has too few rows:", len(rows))
        return 1
    beats = 0
    counted = 0
    for row in rows:
        fraction = float(row[index["retained_fraction"]])
        if not (0.0 < fraction < 1.0):
            continue
        counted += 1
        if float(row[index["rmse"]]) < float(row[index["rmse_random_mean"]]):
            beats += 1
    if counted == 0 or beats != counted:
        print("the confidence curve did not beat random at every row:", beats, "/", counted)
        return 1

    # The committed run is the regression anchor.
    if not compare_to_fixture(out_csv):
        return 1
    fixture_plot = open(os.path.join(FIXTURES, "synthetic_sweep.svg"), "rb").read()
    if plot_bytes.count(b"<polyline") != fixture_plot.count(b"<polyline"):
        print("the plot's curve count drifted from the fixture")
        return 1
    if plot_bytes.count(b"<polygon") != fixture_plot.count(b"<polygon"):
        print("the plot's band count drifted from the fixture")
        return 1

    # The acceptance exit code: 3 when the curve does not beat random.
    failing = run(tool, ["--synthetic", "--expect-beats-random", "--min-improvement", "10.0", "--quiet"])
    if failing.returncode != 3:
        print("an unreachable improvement did not exit 3:", failing.returncode, failing.stderr)
        return 1
    passing = run(tool, ["--synthetic", "--expect-beats-random", "--quiet"])
    if passing.returncode != 0:
        print("the acceptance run did not exit 0:", passing.returncode, passing.stderr)
        return 1

    print("sha256 csv ", hashlib.sha256(csv_bytes).hexdigest())
    print("sha256 plot", hashlib.sha256(plot_bytes).hexdigest())
    print("the artefact test passed")
    for path in (out_csv, out_plot):
        os.remove(path)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: test_artifacts.py <confidence_sweep>")
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
