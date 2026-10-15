# OpenScanner

An open-source macOS/Windows application for Creality (Orbbec-based) 3D
scanners, built as a clean-room replacement for CrealityScan's app layer.

## What this is, concretely

CrealityScan 4.3.2 is a Unity 2022.3.20f1 app (IL2CPP-compiled) that itself
calls into a closed, OEM-licensed scanning SDK from Orbbec ("OBScan" —
shared across multiple scanner brands, not Creality-specific IP) for device
management, calibration, meshing and texturization. Neither CrealityScan nor
the OBScan SDK ship a Linux build.

OpenScanner does **not** attempt to recompile or decompile CrealityScan
itself. Instead it:

1. Independently recovers the OBScan SDK's C ABI (function names, parameter
   types, struct layouts) by decompiling CrealityScan's IL2CPP metadata and
   cross-checking the result against the real exported symbols in the
   shipped `liborbbec_scan` / `lib_orbbec_scan` binaries.
2. Writes fresh, original C++ bindings against that recovered ABI —
   `engine/include/openscanner/obscan_api.h` — the same way any SDK binding
   is written against a third-party interface. No Creality or Orbbec source
   or binary is included in this repository.
3. Builds a new, independent application on top of those bindings.

The OBScan vendor library itself is **not bundled here**. At runtime,
OpenScanner dynamically loads it from an existing CrealityScan installation
on the machine it runs on (or a path you point it at). This project only
ever redistributes code it wrote itself.

## Status

Early scaffolding. `tools/smoke_test` proves the binding approach end-to-end:
it loads the real vendor library, calls `obscan_version()`, creates a
context, and enumerates connected devices — all through our own clean-room
header, no vendor code.

`obscan_api.h` now also covers the session/scan lifecycle needed for live
capture: `obscan_context_create_session` → `obscan_session_create_scan` →
`obscan_scan_start` → `obscan_scan_get_raw_cloud` (poll for point-cloud
data) / `obscan_scan_get_realtime_model_mesh` → `obscan_scan_stop` →
`obscan_session_destroy`, recovered and symbol-verified the same way as
the bootstrap subset. `tools/live_capture_test` exercises the full
sequence against a real device, using the same worker-process isolation
as `smoke_test` since several of these calls are new, unexercised binding
surface. Not yet verified against live data — see "Known issues" below
for why (device enumeration currently returns 0 even with a scanner
connected).

## Known issues

**`obscan_context_destroy()` crashes inside the vendor SDK on macOS (and is
expected to on Windows too; untested).** Confirmed root cause via lldb: the
call internally triggers `texture_mode::shutdown_network()`, which
constructs Creality's `SuperPoint` AI feature-matching network (ncnn +
Vulkan compute) seemingly just to tear it down. That's the first OpenMP
parallel region ever run in a minimal host process, and the vendor's
bundled `libomp.dylib` 5.0 segfaults on a NULL `__kmp_threads` global during
the join barrier — reproduced 100% of the time, on worker threads the
vendor library spawns internally, not just the calling thread. Ruled out as
fixes: `NSApplicationLoad`, `OMP_NUM_THREADS=1`, `KMP_DUPLICATE_LIB_OK=TRUE`,
pre-warming `libomp` directly, and skipping the call entirely (which trades
this crash for a different one — an unjoined background network-discovery
thread aborting on exit).

Because the fault lands on vendor-spawned worker threads, an in-process
signal handler can't recover from it — there is no safe way to resume a
different thread's stack via `siglongjmp`, which is why this isn't "fixed"
with a `try`/`catch`-style guard. The actual mitigation (`tools/smoke_test`
and `engine/include/openscanner/worker_process.h`): the whole
create → enumerate → destroy session runs in a disposable child process,
re-launched from the same executable. If the vendor SDK crashes it, only
that child dies; the parent observes the crash via the exit status and
reports it cleanly. This is a real, verified fix for "our tool crashes" —
it is not a fix for "the vendor SDK's destroy path is broken," which only
Orbbec/Creality can actually fix. The engine will need to use the same
worker-process pattern for any future call into `obscan_context_destroy`
(e.g. on device reconnect), not just at tool shutdown.

The POSIX (macOS) side of `worker_process.cpp` is tested and working. The
Windows side (`CreateProcessW`-based re-exec) is written to the same
design but not yet tested on Windows.

**Device enumeration returns 0 devices even with a scanner physically
connected (a Creality Raptor Pro, confirmed present and recognized by
macOS via `ioreg -p IOUSB` — this is not a cabling/hardware problem).**
The real installer's `postinstall` script sets up `/Library/Creality/`
and a `com.creality.RPCServer` LaunchAgent with a sudoers rule granting it
passwordless root — neither of which exists on a machine where only the
`.pkg` payload was extracted (as this project does) rather than actually
installed. The leading hypothesis is that `liborbbec_scan` talks to that
separate privileged helper process (hence "RPCServer") for real device
access, and silently sees nothing without it. Unverified: starting that
extracted helper directly was blocked by this environment's own safety
tooling (running an unverified vendor background daemon), so the next
step is either a real install of CrealityScan on a test machine, or
running `RPCServer` manually with the user's own authorization. Until
one of those resolves it, the session/scan bindings below are verified
only up to "the real exported symbols exist and the code compiles and
links against them" — not against live capture data.

Scope for v1: OBScan-class (depth-camera) devices only. The separate
MTP-based device path CrealityScan also supports (used by some scanner
models) is deferred.

The app's cloud/account/analytics layer (model upload, device
registration/verification, telemetry) is intentionally **not** part of this
project. OpenScanner is local-only.

## Building

Requires CMake 3.20+ and a C++17 compiler.

```bash
cmake -S . -B build
cmake --build build
```

Run the smoke test against a real install (macOS example):

```bash
./build/tools/smoke_test/smoke_test \
  "/Applications/CrealityScan.app/Contents/PlugIns/liborbbec_scan.dylib"
```

On Windows, point it at
`CrealityScan_Data\Plugins\x86_64\lib_orbbec_scan.dll` inside an existing
install instead.

`tools/live_capture_test` takes the same library path argument and runs
the full session/scan lifecycle against whatever device is connected,
polling and printing point counts for a few seconds.

## Web UI

The UI is a plain web page (`ui/index.html` + `ui/app.js`) — no framework,
no build step, no bundler. It talks to the engine over a local WebSocket
rather than being embedded in a native toolkit, which keeps the render/data
path simple to reason about and lets the same engine serve other local
clients later. There's no native app shell yet (no Electron, no embedded
WebView) — that's deliberately deferred until the content and the
render/latency pipeline are proven out, which is what this milestone does.

`tools/scan_server` streams a synthetic, animated point cloud over that
WebSocket as a stand-in for a real scan session, so the rendering and
latency-measurement pipeline can be built and benchmarked before it's wired
to actual OBScan capture data. Swapping in real frames later changes the
frame producer, not the transport or the UI.

Wire format per frame (binary WebSocket message): `uint32 frame_id`,
`float64 capture_timestamp_ms` (Unix epoch, matching JS `Date.now()`),
`uint32 point_count`, then `point_count * 3` little-endian `float32` (x, y,
z). The transport is a from-scratch, dependency-free WebSocket server
(`engine/include/openscanner/ws_broadcast_server.h`) — binary broadcast
only, no TLS, no fragmentation support; it exists for a localhost data feed
to our own page, not as a general-purpose server.

Rendering is WebGL2 (not WebGPU): broader support today, including Safari,
which matters for a macOS-targeted app, and it's more than fast enough for
point-cloud-scale data. The render loop runs independently of the data
rate (`requestAnimationFrame`, currently ~60fps) so the UI stays smooth
even if capture frames arrive more slowly; the HUD shows both rates
separately (`data` fps vs. `render` fps) plus round-trip latency, since
conflating them would hide exactly the number that matters here. No CSS
transitions or animation beyond the point cloud itself, per the brief:
simple UI, limited animation, latency as the priority metric.

Run it (two processes, both from the repo root):

```bash
./build/tools/scan_server/scan_server        # starts streaming on :8787
python3 -m http.server 8788 --directory ui   # or any static file server
```

Then open `http://127.0.0.1:8788/index.html` in a browser. (Opening
`ui/index.html` directly as a `file://` URL does not reliably run its
script in every environment — serve it over HTTP instead.) Pass
`?port=1234` in the URL if the server isn't on the default 8787.

Verified end-to-end on macOS: WebSocket connects, the point cloud renders
and animates, and the HUD reports ~1–3ms latency and 60fps render / ~27fps
data rate against the synthetic feed on localhost.

## Why C++

Performance over maximal memory safety is the stated priority for this
project, and the surrounding ecosystem (OpenCV, mesh/point-cloud processing,
Orbbec's own SDKs, ONNX Runtime for the AI-assisted mesh completion
CrealityScan already does) is C++-native. Calling the vendor SDK's C ABI
directly, with no FFI bridge in between, also matters here.

## License

MIT — see [LICENSE](LICENSE). This covers OpenScanner's own code only; it
grants no rights to any third-party vendor SDK it may be run alongside.
