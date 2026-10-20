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
import base64
import json
import os
import re
import shutil
import signal
import socket
import struct
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


# ---------------------------------------------------------------------------
# The full-frame mode (row #156)
#
# One frame through the whole pipeline: the synthetic scan_server encodes and
# broadcasts it, and a consumer probe receives it and measures the age - the
# time from the frame's capture stamp to the moment the consumer has it. The
# per-stage split comes from the server's own `[perf] window` line, so the
# total and the stages are the same run.
# ---------------------------------------------------------------------------

V1_MAGIC = 0x4F534346  # 'OSCF'
V1_HEADER = struct.Struct("<IHHIddI")  # magic, version, type, seq, t_capture, t_server, payload_len


def ws_connect(port):
    """A WebSocket client that decodes the V1 frames; (socket, leftover bytes)."""
    sock = socket.create_connection(("127.0.0.1", port), timeout=5.0)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    key = base64.b64encode(os.urandom(16)).decode()
    sock.sendall(("GET / HTTP/1.1\r\n"
                  "Host: 127.0.0.1:%d\r\n"
                  "Upgrade: websocket\r\n"
                  "Connection: Upgrade\r\n"
                  "Sec-WebSocket-Key: %s\r\n"
                  "Sec-WebSocket-Version: 13\r\n\r\n" % (port, key)).encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("the server closed during the handshake")
        buf += chunk
    return sock, buf.split(b"\r\n\r\n", 1)[1]


def read_ws_frame(sock, buf):
    """(opcode, payload, buf) for the next frame; (None, None, buf) if incomplete."""
    if len(buf) < 2:
        return None, None, buf
    opcode = buf[0] & 0x0F
    length = buf[1] & 0x7F
    offset = 2
    if length == 126:
        if len(buf) < 4:
            return None, None, buf
        length = int.from_bytes(buf[2:4], "big")
        offset = 4
    elif length == 127:
        if len(buf) < 10:
            return None, None, buf
        length = int.from_bytes(buf[2:10], "big")
        offset = 10
    if len(buf) < offset + length:
        return None, None, buf
    return opcode, buf[offset:offset + length], buf[offset + length:]


def cpu_seconds(pid):
    """The process's cumulative CPU time in seconds (macOS `ps`)."""
    try:
        out = subprocess.run(["ps", "-o", "time=", "-p", str(pid)], capture_output=True, text=True, timeout=10).stdout
    except Exception:
        return None
    text = out.strip()
    if not text:
        return None
    parts = text.split(":")
    try:
        if len(parts) == 3:
            return int(parts[0]) * 3600 + int(parts[1]) * 60 + float(parts[2])
        if len(parts) == 2:
            return int(parts[0]) * 60 + float(parts[1])
        return float(parts[0])
    except ValueError:
        return None


def run_full_frame(binary, seconds, hz=0, points=0):
    """One frame through the whole pipeline; the per-stage table and the total.

    `hz` and `points` are the preset's knobs. They need the server's
    --synthetic-load mode (#151's, PR #366); with a server that does not take
    them the plain synthetic path runs and the configuration it measured is
    reported, so the mode is never silently wrong.
    """
    port = free_port()
    # The preset's knobs need the server's --synthetic-load mode (#151, PR
    # #366). A server that does not take them is the plain synthetic path,
    # and the report says which configuration was measured.
    commands = []
    if hz > 0 and points > 0:
        commands.append([binary, "--synthetic-load", str(port), str(hz), str(points)])
    commands.append([binary, str(port)])
    proc = None
    sock = None
    buf = b""
    used = None
    for cmd in commands:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, cwd=ROOT)
        time.sleep(1.0)  # the server's accept loop
        if proc.poll() is not None:
            continue  # it rejected the command; try the next one
        try:
            sock, buf = ws_connect(port)
            used = cmd
            break
        except OSError:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
            proc = None
    if proc is None or sock is None:
        if proc is not None:
            proc.kill()
        raise RuntimeError("the synthetic server could not start")
    lines = []
    ages = []
    frames = 0
    bytes_seen = 0
    points = 0
    cpu0 = None
    try:
        cpu0 = cpu_seconds(proc.pid)
        sock.settimeout(1.0)
        deadline = time.time() + seconds
        while time.time() < deadline:
            try:
                chunk = sock.recv(262144)
            except socket.timeout:
                chunk = b""
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            while True:
                opcode, payload, buf = read_ws_frame(sock, buf)
                if opcode is None:
                    break
                if opcode != 0x2 or payload is None or len(payload) < V1_HEADER.size:
                    continue
                magic, version, mtype, seq, t_capture, t_server, payload_len = V1_HEADER.unpack(payload[:V1_HEADER.size])
                if magic != V1_MAGIC:
                    continue
                now_ms = time.time() * 1000.0
                ages.append(now_ms - t_capture)
                frames += 1
                bytes_seen += len(payload)
                if len(payload) >= V1_HEADER.size + 4:
                    points = int.from_bytes(payload[V1_HEADER.size:V1_HEADER.size + 4], "little")
    finally:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass
        cpu1 = cpu_seconds(proc.pid)
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=10)
        out = read_available(proc.stdout)
        if out:
            lines.extend(out.splitlines())
    return {
        "frames": frames,
        "bytes": bytes_seen,
        "points": points,
        "ages": ages,
        "server_lines": lines,
        "command": used,
        "cpu_s": (cpu1 - cpu0) if (cpu0 is not None and cpu1 is not None) else None,
        "live_bytes": live_footprint(proc.pid) if proc.poll() is None else None,
    }


def median(values):
    if not values:
        return 0.0
    s = sorted(values)
    return s[len(s) // 2]


def percentile(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    idx = min(len(s) - 1, int(round((p / 100.0) * (len(s) - 1))))
    return s[idx]


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


# The summary block the record's raw log carries for the recorded dataset:
# "<metric>  min= <a>  median= <b>  max= <c>".
SUMMARY_RE = re.compile(r"^([a-z0-9_.]+)\s+min=\s*([0-9.]+)\s+median=\s*([0-9.]+)\s+max=\s*([0-9.]+)\s*$")

RECORD_LOG = os.path.join(ROOT, "tests", "results",
                          "2026-10-19-perf-contract-budget-harness.raw.log")


def dataset_from_log(path):
    """The per-metric (min, median, max) the log's summary block records."""
    if not os.path.exists(path):
        return None
    out = {}
    with open(path) as f:
        for line in f:
            m = SUMMARY_RE.match(line.rstrip("\n"))
            if m:
                out[m.group(1)] = [float(m.group(2)), float(m.group(3)), float(m.group(4))]
    return out or None


def sync_dataset(contract, path):
    """Copy the log's dataset into the budget file, and recompute each k."""
    data = dataset_from_log(path)
    if not data:
        print("perf-budget: no dataset summary in %s" % path)
        return 1
    contract["dataset"]["measured"] = data
    for b in contract["budgets"]:
        m = b["metric"]
        if m in data:
            b["median_measured"] = data[m][1]
            lo, med, hi = data[m]
            # A metric whose dataset holds a single sample (max == min) has
            # no K to recompute - the file's own value stands, and the
            # budget's derivation says why.
            if med > 0 and hi > lo:
                b["k"] = round(b["max"] / med, 2)
            # The {DATASET} token is filled from the same place, so the
            # derivation's numbers cannot drift from the data.
            unit = b.get("unit", "").split("/")[0]
            fill = "%.3f-%.3f %s (median %.3f)" % (lo, hi, unit, med)
            b["derived_from"] = b["derived_from"].replace("{DATASET}", fill)
    with open(BUDGETS, "w") as f:
        json.dump(contract, f, indent=2)
        f.write("\n")
    print("perf-budget: the budget file's dataset is now the log's")
    for m in sorted(data):
        print("  %-26s min=%9.3f median=%9.3f max=%9.3f" % (m, data[m][0], data[m][1], data[m][2]))
    return 0


def check_dataset(contract, path):
    """The budget file's dataset must be the log's, to the third decimal."""
    data = dataset_from_log(path)
    if not data:
        print("perf-budget: no dataset summary in %s" % path)
        return 1
    recorded = contract.get("dataset", {}).get("measured", {})
    failures = 0
    for m in sorted(set(list(data.keys()) + list(recorded.keys()))):
        if m not in data or m not in recorded:
            print("  %-26s only in one of the two" % m)
            failures += 1
        elif [round(x, 3) for x in recorded[m]] != [round(x, 3) for x in data[m]]:
            print("  %-26s budget file %s vs log %s" % (m, recorded[m], data[m]))
            failures += 1
        else:
            print("  %-26s %s" % (m, data[m]))
    if failures:
        print("perf-budget: the budget file and the log disagree on %d metric(s)" % failures)
        return 1
    print("perf-budget: the budget file's dataset matches the log")
    # The contract quotes the same numbers; check that it does.
    contract_path = os.path.join(ROOT, "docs", "recomp", "PERFORMANCE-CONTRACT.md")
    if os.path.exists(contract_path):
        text = open(contract_path).read()
        missing = []
        for m, (lo, med, hi) in sorted(data.items()):
            if hi <= lo:
                continue  # a single-sample metric: nothing to quote as a range
            if ("%.1f – %.1f" % (lo, hi)) not in text:
                missing.append("%s (%.1f – %.1f)" % (m, lo, hi))
        if missing:
            print("perf-budget: the contract does not quote the dataset for: %s" % ", ".join(missing))
            return 1
        print("perf-budget: the contract quotes the same dataset")
    return 0


def full_frame_mode(contract, build_dir, preset_name, seconds):
    """The full-frame mode: one frame through every stage, timed end to end.

    The per-stage split comes from the server's own instrument and the total
    from the consumer's measurement of the frame's age, so the stages and the
    total are the same run. The total is checked against the preset's
    end-to-end budget: that is the regression threshold (row #156).
    """
    ok, perf_bin, server_bin = ensure_binaries(build_dir, False)
    if not ok:
        return 2
    presets = contract.get("presets", {})
    preset = presets.get(preset_name)
    if not preset:
        print("perf-budget: unknown preset '%s' (known: %s)" % (preset_name, ", ".join(sorted(presets))))
        return 2

    print("== the full-frame run (device-free, one frame through every stage) ==")
    knobs = preset.get("knobs", {})
    result = run_full_frame(server_bin, seconds, int(knobs.get("publish_hz", 0) or 0),
                             int(knobs.get("points", 0) or 0))
    lines = result["server_lines"]
    windows = [l for l in lines if "[perf] window" in l]
    last = windows[-1] if windows else ""

    def seg(name):
        m = re.search(r"%s p50=([0-9.]+)" % re.escape(name), last)
        return float(m.group(1)) if m else 0.0

    post_ms = seg("post")
    bcast_ms = seg("bcast")
    ages = result["ages"]
    age_p50 = median(ages)
    age_p95 = percentile(ages, 95)
    age_max = max(ages) if ages else 0.0
    # The consumer's own time: the age minus the server's own stages.
    transfer_ms = max(0.0, age_p50 - post_ms - bcast_ms)

    print("  frames received      : %d (%d points, %d B)" % (result["frames"], result["points"], result["bytes"]))
    cmd = result.get("command") or []
    if "--synthetic-load" in cmd:
        config = "the preset's knobs (%d points at %d Hz)" % (knobs.get("points", 0), knobs.get("publish_hz", 0))
    else:
        config = ("the plain synthetic path (the server has no --synthetic-load, so the preset's knobs could "
                  "not be set - #151, PR #366)")
    print("  configuration        : %s" % config)
    print("  server CPU           : %s" % ("%.3f s" % result["cpu_s"] if result["cpu_s"] is not None else "n/a"))
    print()
    print("== the per-stage table (one run; the stages and the total are the same run) ==")
    print("%-28s %10s %8s  %s" % ("stage", "value", "unit", "where it comes from"))
    print("%-28s %10.3f %8s  %s" % ("encode + post", post_ms, "ms", "the server's [perf] window: post p50"))
    print("%-28s %10.3f %8s  %s" % ("broadcast", bcast_ms, "ms", "the server's [perf] window: bcast p50"))
    print("%-28s %10.3f %8s  %s" % ("transfer + consume", transfer_ms, "ms", "the age minus the two stages above"))
    print("%-28s %10.3f %8s  %s" % ("TOTAL (the frame age)", age_p50, "ms", "the consumer's measurement: p50"))
    print("%-28s %10.3f %8s  %s" % ("  the age at p95", age_p95, "ms", "the consumer's measurement"))
    print("%-28s %10.3f %8s  %s" % ("  the age at max", age_max, "ms", "the consumer's measurement"))

    # The regression threshold on the total: the preset's frame-age budget.
    budget = preset.get("budgets", {}).get("e2e_frame_age_ms")
    if budget is None:
        budget = contract.get("synthetic", {}).get("e2e_frame_age_ms")
    failures = 0
    print()
    if budget is None:
        print("perf-budget: the preset '%s' has no end-to-end budget" % preset_name)
    else:
        okrow = age_p50 <= float(budget)
        if not okrow:
            failures += 1
        print("%-28s %10.3f %8s  %s" % ("the total's budget", float(budget), "ms",
                                          "PASS" if okrow else "FAIL (the total regressed)"))
    if failures:
        print("perf-budget: the full-frame total REGRESSED")
        return 1
    print("perf-budget: the full-frame total is within the '%s' budget" % preset_name)
    return 0


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
    ap.add_argument("--sync-dataset", action="store_true",
                    help="copy the recorded dataset from the record's raw log into the budget file")
    ap.add_argument("--check-dataset", action="store_true",
                    help="check that the budget file's dataset is the record's raw log's")
    ap.add_argument("--full-frame", action="store_true",
                    help="the full-frame mode (row #156): one frame through every stage, timed end to end")
    ap.add_argument("--preset", default="high",
                    help="the preset whose budgets the full-frame mode asserts (high|balanced|lite|capture-only)")
    args = ap.parse_args()

    if not os.path.exists(BUDGETS):
        print("perf-budget: %s is missing" % BUDGETS)
        return 2
    with open(BUDGETS) as f:
        contract = json.load(f)

    if args.sync_dataset or args.check_dataset:
        if args.sync_dataset:
            return sync_dataset(contract, RECORD_LOG)
        return check_dataset(contract, RECORD_LOG)

    if args.full_frame:
        return full_frame_mode(contract, args.build_dir, args.preset, args.seconds)

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
    # Today's ingest, measured on the device (#141's live split): the same
    # utilisation, with the exposure re-assert still in the tick.
    today_rows = []
    today = allow.get("ingest_measured_today_us_per_frame")
    if today:
        for rate in rates:
            today_rows.append((rate, today * float(rate) / 1000.0 / 10.0))

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
    if today_rows:
        print()
        print("   (today's ingest, measured on the device - #141's live split: the exposure re-assert")
        print("    is still in the tick, and it is 98 % of it)")
        for rate, util in today_rows:
            print("%-6s %10s %10s %11s %14.1f %16s %6s" % (rate, "-", "today", "-", util, "-", "OVER"))

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
