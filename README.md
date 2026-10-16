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
it). The vendor's own scan log shows real per-frame laser points after
`obscan_scan_start` (up to ~900 frames with points in a 30s run), but
`obscan_scan_get_raw_cloud` has returned 0 points during scanning in every
run so far; `live_capture_test` now reads the cloud and realtime mesh once
after `obscan_scan_pause`, the only state the SDK allows the mesh getter
in. See "Known issues" for the teardown/power-cycle story.

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

**Init failures cascading into a scanner that needs a power cycle —
root cause found in the logs, fix untested on hardware.** The symptom:
some runs come up clean and stream data, others fail at preview, and the
failures get worse run over run until `obscan_scan_start()` hangs forever
and only unplugging the scanner helps. Everything before preview (context,
device connect, session, scan) succeeded in every logged run; the
difference is entirely in how the *previous* run ended.

- **`obscan_scan_stop` signature regression (1c17292).** The 3-arg
  `(scan, is_block, status_cb)` shape returns `OB_SCAN_SUCCESS` but does
  nothing: the native log shows `obscan_scan_stop` and then no stop
  progress, no pipeline teardown, and the status callback never fires.
  Both destroys are then refused with `OB_SCAN_ERROR_MULTI_CALLED`
  (`must first wait other process finshed: destroy session !`), so the
  device is never released. The earlier 4-arg
  `(scan, progress_cb, status_cb, is_block)` shape is the real one: with
  it the SDK logs `stop progress 0.01 … 1.0` / `change to stop success !`,
  our progress callback receives those exact values, and session destroy
  releases the device (`rpc client destroyed`). Restored in `obscan_api.h`.
- **Unreleased device → next run degrades.** A worker that exits without
  the SDK's stop/release path (refused destroy, crash, `kill -9`, an early
  `return` on a failed step) leaves the scanner streaming. On the next run
  IR starts instantly (already streaming), frames drop, then IR fails to
  start at all; the preview status callback *still reports success*, no
  `FRAME_READY` ever arrives, and `obscan_scan_start` blocks forever
  waiting for frames. Only a scanner power cycle cleared it in the logs.
- **Process exit aborted (`mutex lock failed`)** after every refused
  destroy, because the worker returned normally: `ObscanLibrary`'s
  destructor `dlclose()`d the SDK under its own running threads, the
  listener structs were stack locals the SDK still pointed at, and static
  destructors ran against live vendor threads.

What `live_capture_test` does about it now: one teardown path for every
exit after `obscan_context_create` (failed call, no device, dead
preview, Ctrl-C, launcher timeout) — pause, stop with the 4-arg shape,
wait for the stop status callback, then destroy with retries on
`MULTI_CALLED`; never calls `obscan_scan_start` on a preview that has
delivered no frames (exits with code 4, "power-cycle the scanner")
instead of hanging; listeners have static lifetime; the library is never
unloaded and the worker leaves via `_Exit`; the launcher sends SIGTERM
(clean stop) before SIGKILL on timeout and lets Ctrl-C reach the worker's
cleanup instead of killing it; a lock file prevents two runs driving the
scanner at once. First hardware run (logs/branch_claude-clever-albattani-
cebx0e/) segfaulted on both attempts right after device connect, in a
`obscan_device_management_get_device_info` call that was only there to
print device details — an unverified binding, never called before, with
no vendor log line for the call. Removed; teardown changes still need
their first real run.

**The Vulkan interposer is not yet shown to do anything.** The three
`live_capture_vulkan_fix_run*.log` runs that were taken as confirmation
never reached the Vulkan code: `obscan_context_destroy()` returned in
~0ms because the SDK *refused* it (`845283600` = `MULTI_CALLED`, the stop
regression above), before it gets as far as constructing SuperPoint; no
MoltenVK activity appears in their native logs, and each worker still
died with `Abort trap: 6`. They did stream without power cycles in
between — those runs ended paused rather than killed mid-stream — but the
third already dropped to 18 frames with laser points (vs ~900), the usual
first sign of the unreleased-device degradation. Also, in the runs where
`obscan_context_destroy()` did reach the MoltenVK hang
(`live_capture_full_run.log`, `live_capture_timeout_test.log`), the
device had already been released (`rpc client destroyed`,
`Context destroyed`) before the hang, so that hang does not by itself
wedge the scanner. The interposer is still loaded by default (opt out
with `OPENSCANNER_NO_VULKAN_INTERPOSE=1`); worth an A/B once teardown is
confirmed clean, since preview itself uses Vulkan in the RGB+IR 50fps
profile.

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
scanning for `OPENSCANNER_SCAN_SECONDS` (default 30) and printing point
counts. Ctrl-C stops the scan and releases the device cleanly (press it
twice to force-kill a stuck worker). Exit codes: 0 done, 1 a vendor call
failed, 3 no scanner, 4 preview delivered no frames (power-cycle the
scanner), 5 interrupted, 6 scanner disconnected, 7 the SDK refused
stop/destroy (scanner may need a power cycle), 2 the worker crashed or
had to be killed.

`tools/fake_obscan` builds a stand-in for the vendor library
(`libfake_obscan.so`/`.dylib`) that models the behaviors above — delayed
device connect, preview-only frames, "cannot called start !", a device
left claimed by an unclean exit — so the capture control flow can be
tested without a scanner:

```bash
FAKE_OBSCAN_SCENARIO=no_frames OPENSCANNER_SCAN_SECONDS=3 \
  ./build/tools/live_capture_test/live_capture_test \
  ./build/tools/fake_obscan/libfake_obscan.so
```

Scenarios: `ok`, `no_device`, `late_device`, `no_frames`, `preview_fail`,
`start_hang`, `destroy_hang` (see `fake_obscan.cpp`).

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
only, no TLS, no fragmentation, no auth; it listens on all interfaces so
it can also be reached from another machine on the LAN, but it still
exists to feed our own page, not as a general-purpose/public server.

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
