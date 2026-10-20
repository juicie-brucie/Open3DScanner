#!/usr/bin/env python3
"""ws_probe — a minimal WebSocket consumer for the row #151 rate probe.

Connects `--clients` sockets, reads frames for `--seconds`, and reports what was
actually received (binary frame count and the realized data fps). This is the
device-free consumer proxy: the browser HUD's own counter would need a browser,
which this harness does not start.

No third-party dependencies; the server sends unmasked frames.
"""

import argparse
import base64
import os
import socket
import struct
import threading
import time

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def connect(host, port, timeout=5):
    s = socket.create_connection((host, port), timeout=timeout)
    key = base64.b64encode(os.urandom(16)).decode()
    req = ("GET / HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
           "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (host, port, key))
    s.sendall(req.encode())
    resp = b""
    while b"\r\n\r\n" not in resp:
        chunk = s.recv(4096)
        if not chunk:
            raise RuntimeError("handshake closed")
        resp += chunk
    if b" 101 " not in resp.split(b"\r\n")[0]:
        raise RuntimeError("handshake failed: " + resp.split(b"\r\n")[0].decode(errors="replace"))
    return s


def read_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise RuntimeError("server closed")
        buf += chunk
    return buf


def read_frame(s):
    h = read_exact(s, 2)
    opcode = h[0] & 0x0F
    masked = h[1] & 0x80
    length = h[1] & 0x7F
    if length == 126:
        length = struct.unpack(">H", read_exact(s, 2))[0]
    elif length == 127:
        length = struct.unpack(">Q", read_exact(s, 8))[0]
    if masked:
        mask = read_exact(s, 4)
        data = bytes(b ^ mask[i % 4] for i, b in enumerate(read_exact(s, length)))
    else:
        data = read_exact(s, length)
    return opcode, data


def client(host, port, seconds, results, idx):
    try:
        s = connect(host, port)
        binary = 0
        text = 0
        nbytes = 0
        t0 = time.time()
        while time.time() - t0 < seconds:
            opcode, data = read_frame(s)
            if opcode == 2:
                binary += 1
                nbytes += len(data)
            elif opcode == 1:
                text += 1
            elif opcode == 8:
                break
        results[idx] = (binary, text, nbytes, time.time() - t0, "")
        s.close()
    except Exception as e:  # noqa: BLE001 - report the failure, do not hide it
        results[idx] = (0, 0, 0, 0.0, str(e))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--clients", type=int, default=1)
    ap.add_argument("--seconds", type=float, default=8.0)
    args = ap.parse_args()

    results = [None] * args.clients
    threads = [threading.Thread(target=client, args=(args.host, args.port, args.seconds, results, i))
               for i in range(args.clients)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    ok = [r for r in results if r is not None and not r[4]]
    errors = [r[4] for r in results if r is not None and r[4]]
    if not ok:
        print("ws_probe: no client succeeded; errors=%s" % (errors or ["(none recorded)"]))
        return 1
    mean_window = sum(r[3] for r in ok) / len(ok)
    mean_frames = sum(r[0] for r in ok)
    fps = mean_frames / mean_window if mean_window else 0.0
    per_client_fps = fps / len(ok)
    print("ws_probe: clients=%d window=%.2fs binary_frames=%d data_fps=%.2f per_client_fps=%.2f bytes=%d%s"
          % (len(ok), mean_window, mean_frames, fps, per_client_fps, sum(r[2] for r in ok),
             (" errors=%s" % errors) if errors else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
