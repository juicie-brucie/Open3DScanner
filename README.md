# OpenScanner

An open-source macOS/Windows application for Creality (Orbbec-based) 3D
scanners, built as a clean-room replacement for CrealityScan's app layer.
Development is starting on MacOS with a Creality Raptor Pro

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
`OB_SCAN_SUCCESS`, **and `obscan_scan_pause` / `obscan_scan_stop` /
`obscan_session_destroy` now complete cleanly too — confirmed by a real
reconstructed mesh (`obscan_scan_get_realtime_model_mesh`, read only
after pause: thousands of real vertices/triangles) and full device
release (`rpc client destroyed` for all four client ports: RGB, IR,
IMU, depth).** Getting there required several things undocumented
anywhere outside CrealityScan's own behavior (see "Known issues" below
for the full story): registering a device-message listener (how this tool receives the
CONNECT/DISCONNECT result; detection starts inside `obscan_context_create()` per PLAN #7), registering data-
and process-message listeners before `obscan_scan_preview` (otherwise
`OB_SCAN_ERROR_LISTENER_NOT_FOUND`), pointing
`ObScanContextConfig::input_algo_dependent_files_dir` at CrealityScan's
own Unity `StreamingAssets` directory (the SDK aborts outright without
it), using `obscan_scan_stop`'s real 4-argument shape — its public C#
wrapper's 2-argument signature is not the native ABI, the same trap as
`obscan_scan_preview` below — and reading the realtime mesh only after
`obscan_scan_pause`, not during active scanning. `obscan_context_destroy`
can still take long enough to exceed a generous worker timeout now that
there's real teardown work for it to do — see "Known issues".

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
device has been through several hard-killed sessions.** The root cause
wasn't a config field (a full isolation pass — `accuracy`,
`light_type`, both voxel-size fields, one at a time, rebuilt and
retested against the physical device each time — ruled all of them
out): it's `obscan_context_destroy()` constructing Creality's
SuperPoint feature-matching network (ncnn + Vulkan/MoltenVK) purely to
immediately tear it down, which on this hardware can hang indefinitely
building a Vulkan device — and that hang happens *before* the vendor
SDK's own USB/RPC device-release steps run, leaving the device in a bad
state that then manifests as `obscan_scan_start()` hanging on the
*next* attempt (not necessarily the same one that hung).
`engine/src/vulkan_disable_interpose.c` interposes MoltenVK's
`vkCreateInstance`/`vkCreateDevice` to fail fast instead, loaded only
into the worker subprocess via `DYLD_INSERT_LIBRARIES`
(`tools/live_capture_test/main.cpp`) — never touching Creality's own
files.

**Status: now genuinely confirmed fixed on `main` itself** (not just on
another branch — see False start #3 for why that distinction matters).
`tools/scan_server --live` ran a full real-hardware session on 2026-10-16
against `main` with the real fix in place: `[vulkan_disable_interpose]
intercepted vkGetInstanceProcAddr lookup of a create function` /
`blocked vkCreateInstance`, `obscan_context_destroy() -> OB_SCAN_SUCCESS`,
worker exited code 0, no power cycle needed. Kept below as a record of
all three mistakes along the way, since the lesson generalizes.

False start #1: three consecutive runs completed with
`obscan_context_destroy()` returning in ~0ms and no power cycle
needed, but a separate bug (below) meant `obscan_scan_stop()` was
silently a no-op in all three: `obscan_session_destroy()` then hit
`OB_SCAN_ERROR_MULTI_CALLED` and refused immediately, *before*
`obscan_context_destroy()` could ever reach the SuperPoint/Vulkan path
at all. A 0ms destroy in those runs proved nothing about the Vulkan
interposer.

False start #2: once `obscan_scan_stop()` was actually fixed,
`obscan_context_destroy()` started exceeding the worker timeout again,
and this was logged as "Vulkan-unrelated" after grepping the native
vendor scan log for `VK_KHR`/`VK_EXT`/`"using metallib"` and finding
nothing. That check proved nothing either: the vendor log never
contains MoltenVK's or ncnn's own stderr output in the first place, so
its absence there says nothing about whether Vulkan was involved —
this project's own full process log for the same run, checked only
later, has a complete MoltenVK extension listing.

The real cause, found by a parallel session working the same bug: the
original `vulkan_disable_interpose.c` only interposed
`vkCreateInstance`/`vkCreateDevice` directly, which only catches calling
code whose references to those exact symbols dyld binds.
`texture_mode::shutdown_network()`'s SuperPoint network runs on ncnn,
which doesn't call `vkCreateInstance` that way: it fetches it at runtime
through `vkGetInstanceProcAddr()`, so the original interposer never
fired against it. The fix also interposes `vkGetInstanceProcAddr()` and
`dlsym()`, and catches `vk_icdGetInstanceProcAddr()` lookups through the
`dlsym()` hook. In the confirming runs the route that actually fired was
the interposed `vkGetInstanceProcAddr()`
(`intercepted vkGetInstanceProcAddr lookup of a create function`); the
`dlsym()` hook never logged an interception, so it's a safety net for
other lookup routes rather than the part that fixed this one.

Hardware-confirmed twice, back-to-back, with no power cycle between
runs: `obscan_context_destroy()` returning `OB_SCAN_SUCCESS` in
729ms/695ms (vs. previously hanging past a 180s timeout), both runs
exiting cleanly (`exit code 0`) with real mesh data (21,672 and 46,218
vertices).

False start #3: that hardware confirmation was real, but it tested the
`claude/clever-albattani-cebx0e` branch's own checkout — the fix was
never actually ported into `main`'s `vulkan_disable_interpose.c`, which
silently kept the old, incomplete version (direct
`vkCreateInstance`/`vkCreateDevice` interposition only). Writing
"genuinely confirmed fixed" into this file on `main`, without checking
that `main`'s own copy of the fixed file actually matched what was
tested, was itself a mistake of the same shape as #1 and #2: validating
one thing and writing the conclusion about another. It surfaced for
real on 2026-10-16 when `tools/scan_server --live` (a new, unrelated
feature, see below) hung at `obscan_context_destroy()` on a stock
`main` checkout, cost a physical power cycle, and the run's own log
showed MoltenVK completing a real `vkCreateInstance`/`vkCreateDevice`
with zero `[vulkan_disable_interpose]` lines anywhere — exactly what
"the old file is still here" looks like. Now ported for real; needs its
own hardware confirmation on `main` specifically before this status
line gets upgraded again.

The underlying lesson, still worth keeping in mind for any future
vendor-SDK crash/hang: prefer letting a hung worker's launcher time out
and request a clean `obscan_scan_stop`/`obscan_context_destroy` over
`kill -9` wherever possible, since the risk isn't our own process
crashing (that part `RunSelfAsWorker`'s timeout already handles) — it's
*hardware/vendor-library state* outliving our process after a hard
kill, which no amount of process-level cleanup on our side can fix
after the fact.

**~~Device enumeration returns 0 devices even with a scanner physically
connected~~ — resolved.** The RPCServer/privileged-helper theory below was
wrong. The real cause: detection is performed by two watchers (libusb USB
hotplug — VIDs `0x2BC5`/`0x1B20` — and a GVCP network watcher) that start
inside `obscan_context_create()` and live wholly in
`libOrbbecSDK.1.7.1.dylib`; IOKit matching registrations are that
library's embedded-libusb darwin backend firing at `UsbContext`
construction, not something this project triggers by registering a
listener. Registering an `ObScanDeviceMessageListener` is how this tool
*receives* the resulting CONNECT/DISCONNECT — it does not start detection.
See `docs/recon/findings/plan7-detection-path.md` and
`obscan_api.h`'s `ObScanDeviceMessage*` block (including the known
post-callback crash hazard, which requires this call site to run inside a
worker process).

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
to actual OBScan capture data.

`scan_server --live [port] [library_path] [session_seconds]` wires it to a
real scan against the physically-connected scanner instead (same
context/session/scan lifecycle as `tools/live_capture_test`, run inside a
worker process for the same crash/hang-isolation reasons). Verified
end-to-end on real hardware 2026-10-16: the UI connects and streams at
~10Hz throughout, real reconstructed mesh data (up to 68,623 vertices seen
so far) broadcasts and renders once per session, and the worker tears down
cleanly (`exit code 0`, no power cycle). Two things worth knowing before
using it:
- The live per-tick frames now come from
  `obscan_scan_get_model_point_color_clouds`, which publishes the model
  cloud while scanning, so they can carry points during the scan. The
  per-tick payload is decimated to a fixed budget so the UI stays live at
  high accumulated point counts. `obscan_scan_get_raw_cloud` is logged
  alongside for contrast (it only reflects the last publish). The final
  `obscan_scan_get_realtime_model_mesh` snapshot is still broadcast after
  pause.
- Ctrl+C triggers a graceful `obscan_scan_pause`/`stop`/`destroy`
  teardown instead of killing the process — **always use Ctrl+C (or let
  the bounded session length finish on its own) rather than `pkill`/
  `kill -9`**, which skips that teardown and has left this hardware
  needing a physical power cycle to recover (confirmed twice).
- If you must kill it, kill the **worker** first, then the launcher:
  `pkill -f "scan_server --live-worker"` (or its PID), then
  `pkill -f "scan_server --live"`. Killing only the launcher leaves the
  worker holding the port and the device — a second SDK user.

Swapping in real frames changes the frame producer, not the transport or
the UI.

Wire format per frame (binary WebSocket message): `uint32 frame_id`,
`float64 capture_timestamp_ms` (Unix epoch, matching JS `Date.now()`),
`uint32 point_count`, then `point_count * 3` little-endian `float32` (x, y,
z). The transport is a from-scratch, dependency-free WebSocket server
(`engine/include/openscanner/ws_broadcast_server.h`) — binary broadcast
only, no TLS, no fragmentation, no auth; it listens on all interfaces so
it can also be reached from another machine on the LAN, but it still
exists to feed our own page, not as a general-purpose/public server.

### Scan controls

The UI's Start/Pause/Finish buttons, auto/manual exposure toggle, and
scanner FOV switch are driven by a small text-frame control protocol on
the same WebSocket connection as the binary point-cloud frames above —
see `tools/scan_server/main.cpp`'s `HandleControlMessage`/
`BuildStatusJson` for the exact (hand-parsed, no JSON library, matching
this file's existing SHA-1/Base64-from-scratch style) shapes. Client ->
server:

```json
{"cmd": "start"}
{"cmd": "pause"}
{"cmd": "finish"}
{"cmd": "set_exposure", "auto": true}
{"cmd": "set_exposure", "auto": false, "value": 650}
{"cmd": "set_fov", "value": "wide"}
```

Server -> client (broadcast on every state change, and once a second
regardless so a freshly-opened tab converges to the current state
without needing an explicit "client just connected" hook):

```json
{"type": "status", "mode": "synthetic", "state": "scanning",
 "autoExposure": false, "exposure": 650, "exposureMin": 1,
 "exposureMax": 1000, "fov": "wide", "fovControllable": true}
```

The UI is purely reactive to these status messages rather than
tracking its own optimistic state, which matters for where this maps
onto real hardware constraints instead of something the UI could just
fake:

- **FOV is a real two-position switch on the scanner, not a
  continuous zoom.** `ObScanFov` (`obscan_api.h`) only has
  `OB_SCAN_FOV_NORMAL`/`OB_SCAN_FOV_WIDE`, set once in
  `ObScanModeConfig` at scan creation — there is no SDK call to change
  it mid-scan. In synthetic mode the control is always live (and maps
  onto the viewer's own camera angle, 45°/75°, as a visual stand-in
  with no hardware behind it). In `--live` mode the server reports
  `fovControllable: false` once scanning has started; the command is
  still accepted but has no effect on that session.
- **Exposure/brightness maps directly onto
  `obscan_scan_config_combined_exposure_and_laser_current`** (range
  [1, 1000]) in live mode, re-asserted every ~100ms tick while in
  manual mode to hold it against the device's own ~15s auto-exposure
  decay (see "Known issues" — there's no dedicated auto/manual switch
  in the ABI, so "auto" here means not re-asserting a value and
  letting that decay happen instead). In synthetic mode there's no
  real sensor; the value instead scales rendered point brightness in
  the shader, purely so the control has a visible effect in the demo.
- **Pause calls an already-proven function**
  (`obscan_scan_pause`, used at end-of-session since before this
  feature existed). **Resuming a paused live scan by calling
  `obscan_scan_start` again is new as of this feature and has not
  been exercised against real hardware before** — this SDK has a
  documented history of needing a physical power cycle after a call
  sequence it doesn't like (see "Known issues"). Test it deliberately
  before relying on it.
- **Finish reuses the exact Ctrl+C teardown path** (sets the same
  `g_stop_requested` flag), so it carries the same already-verified
  pause -> get_realtime_model_mesh -> stop -> session_destroy ->
  context_destroy sequence — no new risk there.

Synthetic mode starts in the `scanning` state immediately on launch,
same as before this feature (so a client-less `scan_server` still
streams a rotating sphere with no UI attached); Start/Pause/Finish let
a connected UI drive it through every state from there.

The camera auto-fits to each incoming frame's data (centroid + ~2-sigma
RMS spread, with the near/far clip planes scaled to match) rather than
using a fixed distance — real scan data is mm-scale with an arbitrary
origin, nothing like the synthetic demo sphere's unit scale, and a fixed
camera/clip-plane setup either renders it as a tiny speck or clips it
out of the frustum entirely depending on the gap between the two scales.

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

A plain static file server like `python3 -m http.server` sends no
cache-control headers, so a browser can silently keep serving a stale
cached `app.js` (and even a stale `index.html`) across reloads and new
tabs, with edits appearing to have no effect — this cost real debugging
time on 2026-10-16 (traced the "fix" through several iterations before
realizing none of them had ever actually loaded). `index.html` now loads
`app.js` with a cache-busting `?t=<timestamp>` query param for this
reason; if a change to `index.html` itself still doesn't seem to apply,
append a throwaway query param to the page URL too (e.g. `&cb=1`), or
hard-reload/open a fresh tab.

To reach it from another machine on the LAN, use the host's address
instead of `127.0.0.1` for both the page and the WebSocket, e.g.
`http://172.22.20.69:8788/index.html` — the page picks up its own
hostname automatically and connects its WebSocket back to the same
host. Pass `?host=172.22.20.69` explicitly if that inference is ever
wrong (e.g. reverse-proxied setups).

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
