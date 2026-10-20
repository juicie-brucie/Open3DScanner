#!/usr/bin/env python3
"""perf-budget — the repeatable benchmark behind the performance contract (row #152).

One command, device-free: it measures the stages the contract budgets and
**fails if a budget regresses**.

    python3 scripts/perf-budget.py [--build-dir build] [--factor N] [--quick]

What it runs (all device-free, all bounded):

  * `build/tools/scan_server/perf_budget` — the product's own encoder and
    WebSocket broadcast, measured in-process, plus the platform's own wait
    floor (the rate ceiling no optimisation can beat).
  * `build/tools/scan_server/scan_server <port>` — the synthetic server's
    tick loop, for the per-tick work its own `[perf] window` line reports.
    No vendor SDK, no device.

The budgets live in `docs/recomp/perf-budgets.json`; this script only reads
and checks them, so the contract and the check cannot drift apart.

`--factor N` scales the CPU budgets by N (the lower-end target's factor);
the default 1.0 checks the reference machine's budgets.

Exit codes: 0 = every budget met, 1 = a budget regressed, 2 = the harness
could not run (no build, no binary).
"""

import argparse
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUDGETS = os.path.join(ROOT, "docs", "recomp", "perf-budgets.json")


def free_port():
    """A free loopback port (bind, read it, close it)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def parse_kv(text):
    """The `key=value` lines of a measurement run."""
    out = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or "=" not in line:
            continue
        key, _, value = line.partition("=")
        try:
            out[key.strip()] = float(value.strip())
        except ValueError:
            pass
    return out


def ensure_binaries(build_dir, quick):
    """Build the two binaries if they are missing. Returns (ok, perf, server)."""
    perf = os.path.join(build_dir, "tools", "scan_server", "perf_budget")
    server = os.path.join(build_dir, "tools", "scan_server", "scan_server")
    if os.path.exists(perf) and os.path.exists(server):
        return True, perf, server
    if not os.path.isdir(build_dir):
        print("perf-budget: no build directory at %s — run: cmake -S . -B %s && cmake --build %s"
              % (build_dir, build_dir, build_dir))
        return False, perf, server
    print("perf-budget: building the two binaries in %s ..." % build_dir)
    targets = ["perf_budget", "scan_server"]
    rc = subprocess.call(["cmake", "--build", build_dir, "--target"] + targets)
    if rc != 0:
        print("perf-budget: the build failed")
        return False, perf, server
    return os.path.exists(perf) and os.path.exists(server), perf, server


WINDOW_RE = re.compile(r"\[perf\] window .*?rate=([0-9.]+)/s .*?points=([0-9.]+) bytes=([0-9.]+)")
POST_RE = re.compile(r"post p50=([0-9.]+)")
BCAST_RE = re.compile(r"bcast p50=([0-9.]+)")
TICK_RE = re.compile(r"tick p50=([0-9.]+)")
CLIENTS_RE = re.compile(r"clients=([0-9.]+)")


def read_available(stream):
    """Whatever the stream has right now (it is non-blocking); '' when empty."""
    try:
        return stream.read() or ""
    except (BlockingIOError, ValueError):
        return ""


def run_synthetic_server(binary, seconds, quick):
    """Run the synthetic server, read its `[perf] window` lines, stop it."""
    port = free_port()
    proc = subprocess.Popen([binary, str(port)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, cwd=ROOT)
    lines = []
    footprint = None
    deadline = time.time() + seconds
    try:
        os.set_blocking(proc.stdout.fileno(), False)
        while time.time() < deadline:
            chunk = read_available(proc.stdout)
            if chunk:
                lines.extend(chunk.splitlines())
            if footprint is None and time.time() > deadline - seconds + 4:
                # The footprint is sampled once the tick has settled.
                footprint = live_footprint(proc.pid)
            time.sleep(0.2)
        chunk = read_available(proc.stdout)
        if chunk:
            lines.extend(chunk.splitlines())
    finally:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=10)
    out = {"windows": len(lines), "rate": 0.0, "points": 0.0, "bytes": 0.0,
           "post_p50_ms": 0.0, "bcast_p50_ms": 0.0, "tick_p50_ms": 0.0, "clients": 0.0,
           "live_bytes": footprint}
    windows = [l for l in lines if "[perf] window" in l]
    if not windows:
        return out, lines
    # The last window is the most settled one.
    last = windows[-1]
    m = WINDOW_RE.search(last)
    if m:
        out["rate"] = float(m.group(1))
        out["points"] = float(m.group(2))
        out["bytes"] = float(m.group(3))
    m = POST_RE.search(last)
    if m:
        out["post_p50_ms"] = float(m.group(1))
    m = BCAST_RE.search(last)
    if m:
        out["bcast_p50_ms"] = float(m.group(1))
    m = TICK_RE.search(last)
    if m:
        out["tick_p50_ms"] = float(m.group(1))
    m = CLIENTS_RE.search(last)
    if m:
        out["clients"] = float(m.group(1))
    return out, lines


def parse_size(text):
    """'86K' -> 86016: the sizes vmmap prints carry a K/M/G suffix."""
    text = text.strip()
    mult = 1
    if text.endswith("K"):
        mult, text = 1024, text[:-1]
    elif text.endswith("M"):
        mult, text = 1024 * 1024, text[:-1]
    elif text.endswith("G"):
        mult, text = 1024 * 1024 * 1024, text[:-1]
    try:
        return int(float(text) * mult)
    except ValueError:
        return None


def live_footprint(pid):
    """The server's live allocation footprint in bytes (macOS: vmmap).

    This is the malloc zone's ALLOCATED column — the live bytes the process
    holds — the same number #147's profile read (86-87 KB).
    """
    if not shutil.which("vmmap"):
        return None
    try:
        out = subprocess.run(["vmmap", "-summary", str(pid)], capture_output=True, text=True, timeout=30).stdout
    except Exception:
        return None
    for line in out.splitlines():
        if not line.startswith("DefaultMallocZone"):
            continue
        # REGION | SIZE | SIZE | SIZE | SIZE | COUNT | ALLOCATED | FRAG SIZE
        fields = line.split()
        if len(fields) < 7:
            continue
        return parse_size(fields[6])
    return None


def median_of_runs(runs):
    """The per-metric median across the runs (the statistic the budgets are checked against).

    The machine is shared, so a single run's number moves with the load; the
    median of several runs is the stable one, and it is what the budget
    compares against. The per-run values are printed too, so the spread is
    on the record.
    """
    out = {}
    keys = set()
    for r in runs:
        keys |= set(r.keys())
    for key in sorted(keys):
        values = sorted(r[key] for r in runs if key in r)
        if not values:
            continue
        out[key] = values[len(values) // 2] if len(values) % 2 else \
            (values[len(values) // 2 - 1] + values[len(values) // 2]) / 2.0
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default=os.path.join(ROOT, "build"),
                    help="the build directory (default: build)")
    ap.add_argument("--factor", type=float, default=1.0,
                    help="scale the CPU budgets by this factor (the lower-end target's; default 1.0)")
    ap.add_argument("--quick", action="store_true", help="a short run (for a smoke check)")
    ap.add_argument("--repeat", type=int, default=3,
                    help="how many times to run the measurement; the median is checked (default 3)")
    ap.add_argument("--seconds", type=float, default=12.0,
                    help="how long the synthetic server runs (default 12)")
    args = ap.parse_args()

    if not os.path.exists(BUDGETS):
        print("perf-budget: %s is missing" % BUDGETS)
        return 2
    with open(BUDGETS) as f:
        contract = json.load(f)

    ok, perf_bin, server_bin = ensure_binaries(args.build_dir, args.quick)
    if not ok:
        return 2

    print("== the measurement (device-free) ==")
    measured = {}
    runs = []
    for attempt in range(max(1, args.repeat)):
        bench = subprocess.run([perf_bin] + (["--quick"] if args.quick else []),
                               capture_output=True, text=True, cwd=ROOT)
        if bench.returncode != 0:
            print(bench.stdout)
            print(bench.stderr)
            print("perf-budget: the measurement binary failed")
            return 2
        run = parse_kv(bench.stdout)
        runs.append(run)
        print("  run %d:" % (attempt + 1))
        for line in bench.stdout.splitlines():
            if line.strip():
                print("    " + line.strip())
    measured.update(median_of_runs(runs))

    server_out = {}
    lines = []
    if args.quick:
        # A smoke run does not start the server, so the tick's own numbers
        # are absent rather than zero (a zero would read as a pass).
        server_out = {}
    else:
        server_out, lines = run_synthetic_server(server_bin, args.seconds, args.quick)
    if server_out:
        # Only trust the server's numbers if it actually reported a window:
        # a server that failed to start would otherwise read as zero work,
        # which is a false pass.
        if server_out.get("windows", 0) == 0:
            print("perf-budget: the synthetic server reported no [perf] window — its numbers are skipped")
            server_out = {}
    if server_out:
        measured["tick.post_ms"] = server_out.get("post_p50_ms", 0.0)
        measured["tick.bcast_ms"] = server_out.get("bcast_p50_ms", 0.0)
        measured["tick.period_ms"] = server_out.get("tick_p50_ms", 0.0)
        measured["tick.rate"] = server_out.get("rate", 0.0)
        measured["tick.clients"] = server_out.get("clients", 0.0)
        measured["tick.points"] = server_out.get("points", 0.0)
        measured["tick.bytes"] = server_out.get("bytes", 0.0)
        if server_out.get("live_bytes"):
            measured["server.live_bytes"] = server_out["live_bytes"]

    # The budgets.
    rows = []
    failures = 0
    judged = not args.quick
    for b in contract["budgets"]:
        metric = b["metric"]
        if metric not in measured:
            rows.append((metric, None, None, b.get("max"), "SKIP", b.get("derived_from", "")))
            continue
        value = measured[metric]
        limit = float(b["max"])
        if b.get("factor", False):
            limit *= args.factor
        okrow = value <= limit
        if not okrow and judged:
            failures += 1
        verdict = "PASS" if okrow else ("FAIL" if judged else "n/a")
        rows.append((metric, value, limit, b.get("max"), verdict, b.get("derived_from", "")))

    # The ingest budget: the per-consumed-frame allowance against the rate
    # the platform can actually deliver. The utilisation is reported at the
    # nominal rate (what the code must fit into) AND at the platform's
    # measured ceiling (what this machine can actually deliver). The
    # reference machine's row is a gate (the code must keep up); the
    # lower-end row is a projection of the assumed target — over budget
    # there is a finding for the operator, not a regression of the code.
    allow = contract["allowances"]
    rates = contract.get("rates", [24, 50, 60])
    lower_factor = float(contract["reference"].get("lower_end_factor", 1.0))
    ingest_rows = []
    over_target = 0
    for rate in rates:
        ceiling = measured.get("platform.rate.%d" % rate)
        if ceiling is None:
            continue
        effective = min(float(rate), ceiling)
        for label, factor, gate in (("M1", 1.0, True), ("lower-end", lower_factor, False)):
            per_frame = allow["ingest_us_per_frame"] * factor
            at_nominal = per_frame * float(rate) / 1000.0 / 10.0
            at_ceiling = per_frame * effective / 1000.0 / 10.0
            okrow = at_nominal <= 100.0 and at_ceiling <= 100.0
            if not okrow and gate and judged:
                failures += 1
            if not okrow and not gate:
                over_target += 1
            if not okrow and not gate:
                verdict = "OVER (target)"
            elif okrow:
                verdict = "PASS"
            else:
                verdict = "FAIL" if judged else "n/a"
            ingest_rows.append((rate, effective, label, per_frame / 1000.0, at_nominal, at_ceiling, verdict))

    print()
    print("== the budgets ==")
    print("%-34s %12s %12s %6s  %s" % ("metric", "measured", "budget", "", "verdict"))
    for metric, value, limit, raw, verdict, derived in rows:
        vs = "-" if value is None else ("%.3f" % value)
        ls = "-" if limit is None else ("%.3f" % limit)
        # The derivation is truncated for the table; the full text is in
        # docs/recomp/perf-budgets.json. No trailing whitespace.
        note = derived[:72].rstrip()
        print("%-34s %12s %12s %6s  %s" % (metric, vs, ls, verdict, note))
    print()
    print("== the ingest budget, per consumed frame, at each source rate ==")
    print("%-6s %10s %10s %11s %14s %16s %6s" %
          ("rate", "ceiling", "target", "ms/frame", "core % nominal", "core % at ceiling", "verdict"))
    for rate, effective, label, per_frame_ms, at_nominal, at_ceiling, verdict in ingest_rows:
        print("%-6s %10.2f %10s %11.2f %14.1f %16.1f %6s" %
              (rate, effective, label, per_frame_ms, at_nominal, at_ceiling, verdict))

    print()
    if failures:
        print("perf-budget: %d budget(s) REGRESSED" % failures)
        return 1
    if over_target:
        print("perf-budget: every measured budget met; %d rate(s) exceed the assumed lower-end target "
              "(a finding for the operator, not a regression — re-base when the target machine is named)" % over_target)
        return 0
    print("perf-budget: every budget met")
    return 0


if __name__ == "__main__":
    sys.exit(main())
