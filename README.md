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
`obscan_scan_preview` → `obscan_scan_start` → `obscan_scan_get_raw_cloud`
(poll for point-cloud data) / `obscan_scan_get_realtime_model_mesh` →
`obscan_scan_stop` → `obscan_session_destroy`, recovered and symbol-
verified the same way as the bootstrap subset. `tools/live_capture_test`
exercises the full sequence against a real device, using the same
worker-process isolation as `smoke_test`.

Verified end-to-end against a physical CR-Scan Raptor Pro: device
enumeration, session/scan creation, preview, and start all return
`OB_SCAN_SUCCESS`. Getting there required three things undocumented
anywhere outside CrealityScan's own behavior (see "Known issues" below
for the full story): registering a device-message listener (the actual
trigger for the vendor SDK's device-watcher thread), registering data-
and process-message listeners before `obscan_scan_preview` (otherwise
`OB_SCAN_ERROR_LISTENER_NOT_FOUND`), and pointing
`ObScanContextConfig::input_algo_dependent_files_dir` at CrealityScan's
own Unity `StreamingAssets` directory (the SDK aborts outright without
it). Teardown (`obscan_scan_stop` / `obscan_context_destroy`) is still
flaky, and getting a non-zero point count from `obscan_scan_get_raw_cloud`
in practice is tracked as follow-up work — see "Known issues".

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

**`obscan_scan_start()` can hang indefinitely (not crash) after the USB
device has been through several hard-killed sessions.** During testing,
after `kill -9`ing the worker process mid-stream several times in a row
(necessary when iterating on config — the worker was still inside vendor
SDK code with the laser active each time), `obscan_scan_preview()` stopped
producing *any* data messages at all (not even `NEW_RGB_IMAGE`), and the
subsequent `obscan_scan_start()` call hung forever at low CPU, stuck right
after the native log line `"set ir line laser control fill light, blue
light enable:1"`. A full isolation pass — varying `accuracy`, `light_type`,
and both voxel-size fields one at a time, rebuilding and retesting against
the physical device each time, including restoring the exact combination
that had worked minutes earlier — ruled out every `ObScanModeConfig` field
as the cause. This points at the vendor SDK (or the USB stack's view of the
device) being left in a bad state by the improper teardown a `SIGKILL`
causes — it never runs the SDK's own close/release path. Try a physical
USB reconnect (or OS sleep/wake) before chasing this as a config bug. The
underlying lesson: prefer letting a hung worker's launcher time out and
request a clean `obscan_scan_stop`/`obscan_context_destroy` over `kill -9`
wherever possible; this is also a case `RunSelfAsWorker` doesn't currently
protect against, since the risk here isn't our process crashing, it's the
*hardware/vendor-library state* outliving our process after a hard kill.

**~~Device enumeration returns 0 devices even with a scanner physically
connected~~ — resolved.** The RPCServer/privileged-helper theory below was
wrong. The real cause: `obscan_context_create()` and
`obscan_context_get_device_management()` don't start the vendor SDK's
IOKit device-watcher thread by themselves — confirmed by Frida-tracing
`IOServiceAddMatchingNotification`/`CFRunLoopAddSource` against our own
process and seeing zero registrations without a listener. The watcher
thread only starts as a side effect of registering a real
`ObScanDeviceMessageListener` via
`obscan_device_management_add_device_msg_listener()` (see
`obscan_api.h`'s `ObScanDeviceMessage*` block for the full story,
including the known post-callback crash hazard this call sites must run
inside a worker process).

Needed no privileged helper, no real install, nothing beyond what this
project already does. Superseded the following, kept only as a record
of a plausible-looking wrong turn: the real installer's `postinstall`
script sets up `/Library/Creality/` and a `com.creality.RPCServer`
LaunchAgent with a sudoers rule granting it passwordless root, which
doesn't exist on a machine where only the `.pkg` payload was extracted
rather than installed — this looked like the cause, but wasn't.

Scope for v1: OBScan-class (depth-camera) devices only. The separate
MTP-based device path CrealityScan also supports (used by some scanner
models) is deferred.

The app's cloud/account/analytics layer (model upload, device
registration/verification, telemetry) is intentionally **not** part of this
project. OpenScanner is local-only.

## Possible future direction: dropping OBScan entirely

The vendor stack actually splits into two layers, and only one of them is
closed. The low-level camera/USB layer (`libOrbbecSDK.1.7.1.dylib`) is
Orbbec's own **public, open-source** SDK — the same one published on
GitHub for their other camera lines, built on `rpclib`/msgpack-RPC, not
Creality- or CrealityScan-specific code. Only `liborbbec_scan` (OBScan) —
the scanning-specific depth fusion, laser calibration (`camera_user_calib`,
a separate OpenCV-based tool bundled in the app), and mesh pipeline built
*on top of* that open SDK — is the closed, OEM-licensed part this project
currently binds against.

That means there's a real, scoped alternative to reverse-engineering
OBScan's ABI if its remaining bugs (see "Known issues") turn out to be
unfixable from the outside, or if full independence from Creality/Orbbec's
closed code becomes a goal in its own right: talk to the Raptor Pro
directly via the public OrbbecSDK for raw stereo IR/depth frames (bypassing
`obscan_*` entirely), and implement our own laser-line triangulation and
point-cloud fusion on top — open-source and auditable, instead of a
black-box reimplementation of Creality's algorithms. This is a materially
bigger undertaking than the current binding approach (it means owning
calibration and depth reconstruction, not just device I/O and lifecycle
management), so it's not the current plan — but it's the right next step
if OBScan's own bugs prove unfixable, not a full rewrite starting from raw
USB protocol work.

Not yet verified: whether the public OrbbecSDK's device list actually
includes the Raptor Pro's PID (`0x0c03`/`3075`) as supported.

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
