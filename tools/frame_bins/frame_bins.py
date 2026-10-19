#!/usr/bin/env python3
"""frame_bins — deterministic extractor for the captured RPC frame bins (row #123).

Reads a capture (the `small_payloads.tsv` digest, or a pcapng as fallback),
reassembles the msgpack-RPC responses, and writes the `ob_rpc_get_frame`
payloads per device port plus an index. The output layout is documented in
README.md next to this file; recon's container identification and the decoder
consume exactly those files.

Usage:
    frame_bins.py --input SESSION4/small_payloads.tsv --out DIR
    frame_bins.py --input SESSION4/session4.pcapng --out DIR --format pcap
    frame_bins.py --input FIXTURE.tsv --out DIR --method all

Exit codes: 0 = wrote the output, 1 = a check/parse failure, 2 = bad arguments.

Determinism: the same input produces the same file names and bytes. Sequence
numbers are per-port ordinals in capture order; file names carry no timestamps.
"""

import argparse
import collections
import os
import struct
import sys

DEVICE_PORTS = {"10010": "depth", "10013": "ir"}

# --- msgpack (complete enough for the rpc stream: bins, str, ext, maps) -----

class Need(Exception):
    """More bytes are required to finish the value."""


def unpack(buf, pos=0):
    if pos >= len(buf):
        raise Need
    t = buf[pos]

    def take(n):
        if pos + 1 + n > len(buf):
            raise Need
        return buf[pos + 1:pos + 1 + n]

    if t <= 0x7F:
        return t, pos + 1
    if t >= 0xE0:
        return t - 256, pos + 1
    if 0x80 <= t <= 0x8F:
        d = {}
        j = pos + 1
        for _ in range(t & 0x0F):
            k, j = unpack(buf, j)
            v, j = unpack(buf, j)
            d[k] = v
        return d, j
    if 0x90 <= t <= 0x9F:
        a = []
        j = pos + 1
        for _ in range(t & 0x0F):
            v, j = unpack(buf, j)
            a.append(v)
        return a, j
    if 0xA0 <= t <= 0xBF:
        n = t & 0x1F
        return bytes(take(n)), pos + 1 + n
    if t == 0xC0:
        return None, pos + 1
    if t == 0xC2:
        return False, pos + 1
    if t == 0xC3:
        return True, pos + 1
    if t in (0xC4, 0xD9):
        n = take(1)[0]
        start = pos + 2
    elif t in (0xC5, 0xDA):
        n = struct.unpack(">H", take(2))[0]
        start = pos + 3
    elif t in (0xC6, 0xDB):
        n = struct.unpack(">I", take(4))[0]
        start = pos + 5
    elif t in (0xC7, 0xC8, 0xC9, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8):
        if t == 0xC7:
            n = take(1)[0]
            start = pos + 2
        elif t == 0xC8:
            n = struct.unpack(">H", take(2))[0]
            start = pos + 3
        elif t == 0xC9:
            n = struct.unpack(">I", take(4))[0]
            start = pos + 5
        else:
            n = {0xD4: 1, 0xD5: 2, 0xD6: 4, 0xD7: 8, 0xD8: 16}[t]
            start = pos + 1
        if start + 1 + n > len(buf):
            raise Need
        return ("ext", buf[start], bytes(buf[start + 1:start + 1 + n])), start + 1 + n
    else:
        n = None
        start = None
    if n is not None:
        if start + n > len(buf):
            raise Need
        return bytes(buf[start:start + n]), start + n
    scalars = {0xCA: (">f", 4), 0xCB: (">d", 8), 0xCC: (">B", 1), 0xCD: (">H", 2),
               0xCE: (">I", 4), 0xCF: (">Q", 8), 0xD0: (">b", 1), 0xD1: (">h", 2),
               0xD2: (">i", 4), 0xD3: (">q", 8)}
    if t in scalars:
        fmt, size = scalars[t]
        return struct.unpack(fmt, take(size))[0], pos + 1 + size
    if t in (0xDC, 0xDD):
        n = struct.unpack(">H" if t == 0xDC else ">I", take(2 if t == 0xDC else 4))[0]
        j = pos + (3 if t == 0xDC else 5)
        a = []
        for _ in range(n):
            v, j = unpack(buf, j)
            a.append(v)
        return a, j
    if t in (0xDE, 0xDF):
        n = struct.unpack(">H" if t == 0xDE else ">I", take(2 if t == 0xDE else 4))[0]
        j = pos + (3 if t == 0xDE else 5)
        d = {}
        for _ in range(n):
            k, j = unpack(buf, j)
            v, j = unpack(buf, j)
            d[k] = v
        return d, j
    raise ValueError("unsupported 0x%02x" % t)

# --- sources ---------------------------------------------------------------

def rows_from_tsv(path):
    """Yield (timestamp, src, dst, payload) for every digest row."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 4:
                continue
            ts, sp, dp, hexpay = parts
            try:
                payload = bytes.fromhex(hexpay)
            except ValueError:
                continue
            yield ts, sp, dp, payload


def _tcp_rows_from_pcap(path):
    """Yield (timestamp, src, dst, seq, payload) for IPv4/TCP packets.

    Handles LINKTYPE_NULL (BSD loopback, 4-byte family) and Ethernet.
    """
    with open(path, "rb") as f:
        linktype = None
        while True:
            hdr = f.read(8)
            if len(hdr) < 8:
                return
            btype, blen = struct.unpack("<II", hdr)
            body = f.read(blen - 8)
            if btype == 1:  # IDB
                linktype = struct.unpack_from("<H", body, 0)[0]
            elif btype == 6:  # EPB
                iface, tsh, tsl, caplen, _orig = struct.unpack_from("<IIIII", body, 0)
                pkt = body[20:20 + caplen]
                if linktype == 0:
                    if len(pkt) < 4 or pkt[:4] != b"\x02\x00\x00\x00":
                        continue
                    ip = pkt[4:]
                elif linktype == 1:
                    if len(pkt) < 14 or pkt[12:14] != b"\x08\x00":
                        continue
                    ip = pkt[14:]
                else:
                    continue
                if len(ip) < 20 or ip[9] != 6:
                    continue
                ihl = (ip[0] & 0x0F) * 4
                total = struct.unpack_from(">H", ip, 2)[0]
                tcp = ip[ihl:total]
                if len(tcp) < 20:
                    continue
                sp, dp, seq = struct.unpack_from(">HHI", tcp, 0)
                doff = (tcp[12] >> 4) * 4
                payload = tcp[doff:]
                if payload:
                    yield (tsh << 32) | tsl, str(sp), str(dp), seq, payload


def rows_from_pcap(path):
    """Reassemble each direction by TCP sequence; yield complete payload runs.

    A sequence gap ends the run (the capture can be lossy); the next segment
    starts a new run, so a split message is dropped rather than spliced from
    non-adjacent bytes.
    """
    streams = collections.defaultdict(dict)  # (src,dst) -> seq -> (ts, bytes)
    for ts, sp, dp, seq, payload in _tcp_rows_from_pcap(path):
        if sp in DEVICE_PORTS or dp in DEVICE_PORTS:
            streams[(sp, dp)][seq] = (ts, payload)
    for (sp, dp), segs in streams.items():
        prev_end = None
        run = []
        run_ts = None
        for seq in sorted(segs):
            ts, data = segs[seq]
            if run and seq > prev_end:
                # A gap ends the contiguous run: emit it, start fresh (a message
                # split across the gap is dropped by the parser, not spliced).
                yield run_ts, sp, dp, b"".join(run)
                run = []
                run_ts = None
            elif run and seq < prev_end:
                data = data[prev_end - seq:]  # trim a retransmitted prefix
            if not run:
                run_ts = ts
            run.append(data)
            prev_end = seq + len(data)
        if run:
            yield run_ts, sp, dp, b"".join(run)

# --- extraction ------------------------------------------------------------

def classify(rows):
    """Walk rows in capture order and yield (port, ts, method, payload).

    Requests (`[0, msgid, method, params]`) build the per-connection msgid map;
    responses (`[1, msgid, nil, bin]`) are resolved through it.
    """
    methods = collections.defaultdict(dict)  # (dev, client) -> msgid -> method
    for ts, sp, dp, payload in rows:
        dev = sp if sp in DEVICE_PORTS else (dp if dp in DEVICE_PORTS else None)
        if dev is None:
            continue
        client = dp if sp == dev else sp
        pos = 0
        while pos < len(payload):
            try:
                value, end = unpack(payload, pos)
            except Need:
                break                      # incomplete tail (a split message) — drop it
            except (ValueError, IndexError, TypeError):
                pos += 1                   # resync past interleaved non-msgpack bytes
                continue
            pos = end
            if not (isinstance(value, list) and len(value) == 4):
                continue
            mid = value[1]
            if value[0] == 0 and isinstance(mid, int) and isinstance(value[2], (bytes, bytearray)):
                methods[(dev, client)][mid] = value[2].decode("utf-8", "replace")
            elif value[0] == 1 and isinstance(mid, int) and value[2] is None and isinstance(value[3], (bytes, bytearray)):
                yield dev, ts, methods[(dev, client)].get(mid, "?"), value[3]


def run(args):
    rows = rows_from_pcap(args.input) if args.format == "pcap" else rows_from_tsv(args.input)

    wanted = None if args.method == "all" else args.method
    counts = collections.Counter()
    bytes_total = collections.Counter()
    index = []
    sequences = collections.Counter()

    os.makedirs(args.out, exist_ok=True)
    for dev, ts, method, payload in classify(rows):
        counts[("responses", dev)] += 1
        if wanted is not None and not method.startswith(wanted):
            continue
        if len(payload) < args.min_bytes:
            continue
        sequences[dev] += 1
        seq = sequences[dev]
        label = DEVICE_PORTS[dev]
        port_dir = os.path.join(args.out, label)
        os.makedirs(port_dir, exist_ok=True)
        name = "%06d.bin" % seq
        with open(os.path.join(port_dir, name), "wb") as f:
            f.write(payload)
        counts[("written", dev)] += 1
        bytes_total[dev] += len(payload)
        index.append((label, seq, ts, len(payload), method, args.input, name))

    index_path = os.path.join(args.out, "index.tsv")
    with open(index_path, "w", encoding="utf-8") as f:
        f.write("port\tsequence\ttimestamp\tbyte_length\tmethod\tsource\tfile\n")
        for row in index:
            f.write("\t".join(str(x) for x in row) + "\n")

    print("frame_bins: input=%s format=%s method=%s" % (args.input, args.format, args.method))
    for dev, label in sorted(DEVICE_PORTS.items()):
        print("  %-5s %-5s responses=%-6d written=%-6d bytes=%d"
              % (dev, label, counts[("responses", dev)], counts[("written", dev)], bytes_total[dev]))
    print("  index: %s (%d rows)" % (index_path, len(index)))
    return 0

# --- entry -----------------------------------------------------------------

def main(argv):
    ap = argparse.ArgumentParser(description="deterministic RPC frame-bin extractor (row #123)")
    ap.add_argument("--input", required=True, help="small_payloads.tsv, or a .pcapng with --format pcap")
    ap.add_argument("--out", required=True, help="output directory (keep it outside the repo)")
    ap.add_argument("--format", choices=("tsv", "pcap"), default="tsv")
    ap.add_argument("--method", default="ob_rpc_get_frame",
                    help="method prefix to extract (default ob_rpc_get_frame; 'all' = every bin result)")
    ap.add_argument("--min-bytes", type=int, default=0, help="skip bins smaller than this")
    args = ap.parse_args(argv)
    return run(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
