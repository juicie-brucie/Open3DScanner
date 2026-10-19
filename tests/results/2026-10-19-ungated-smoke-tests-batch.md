# tracking_status_test / bounded_watchdog_test / mesh_tools_smoke_test / smoke_test — runs under their own name — PASS (3) / PASS-with-known-recovered-crash (1)

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Preflight/postflight: not run for the first three (genuinely hardware-free —
  no vendor SDK linked, no subprocess, no device I/O — per their own
  CMake/header comments and confirmed by reading each). A manual freshness
  check was done by hand instead (see below) since none of the four names
  appear in `scripts/hw-preflight.sh`'s gated target list. For `smoke_test` I
  additionally ran `scripts/hw-postflight.sh` after the run — see its own
  section — because, unlike the other three, it dlopen's the real vendor SDK
  and forks a worker that touches the actual device at runtime (confirmed by
  reading its own header comment and observing its output), so a crash-report
  sweep was warranted despite the "hardware-free" build-time link list.
- Interposer: n/a for all four — none of them call `setenv("DYLD_INSERT_LIBRARIES", ...)` (confirmed by reading each main.cpp); none printed a `[launcher] DYLD_INSERT_LIBRARIES=...` or `[vulkan_disable_interpose] loaded` line.

## 1. tracking_status_test

- Target: `build/tools/scan_server/tracking_status_test` (binary mtime 2026-10-19 00:07:09 >= source `tracking_status_test.cpp` 00:07:07; this binary has no separate source subdirectory, so freshness was checked by hand against its own `.cpp`/`.h`, not `tools/scan_server/main.cpp`'s later changes which belong to the unrelated `scan_server` target in the same CMakeLists.)
- Command: `build/tools/scan_server/tracking_status_test`
- Exit code: `0`
- Raw log: `2026-10-19-tracking-status-test.raw.log`
- Output: `[tracking_status_test] PASS: 7 declared values named, out-of-range -> UNKNOWN, status+counts JSON OK`
- Verdict: **PASS**

## 2. bounded_watchdog_test

- Target: `build/tools/scan_server/bounded_watchdog_test` (binary mtime 2026-10-19 02:41:08 >= source `bounded_watchdog_test.cpp`/`bounded_watchdog.h` 02:40:32; same note as above re: `main.cpp`'s unrelated later mtime.)
- Command: `build/tools/scan_server/bounded_watchdog_test`
- Exit code: `0`
- Raw log: `2026-10-19-bounded-watchdog-test.raw.log`
- Output: `[bounded_watchdog_test] PASS: deterministic — disarmed never fires, fires only past the grace, a fired bound is consumed, re-arm is a fresh bound`
- Verdict: **PASS**

## 3. mesh_tools_smoke_test

- Target: `build/tools/mesh_tools_smoke_test/mesh_tools_smoke_test` (binary mtime 2026-10-17 19:20:48 >= source `main.cpp` 18:49:32)
- Command: `build/tools/mesh_tools_smoke_test/mesh_tools_smoke_test`
- Exit code: `0`
- Raw log: `2026-10-19-mesh-tools-smoke-test.raw.log`
- Output:
  ```
  box_a: 8 verts / 12 tris, box_b: 8 verts / 12 tris
  box_a - box_b: 16 verts / 24 tris
  decimated to target 12 tris: 10 verts / 12 tris
  OK: Manifold boolean + libigl decimate both wired up correctly
  ```
- Verdict: **PASS**

## 4. smoke_test

- Target: `build/tools/smoke_test/smoke_test` (binary mtime 2026-10-18 21:46:44 >= source `main.cpp` 20:10:40)
- Command: `build/tools/smoke_test/smoke_test`
- Run start: 2026-10-19 16:42:02
- Exit code: `3`
- Raw log: `2026-10-19-smoke-test.raw.log`
- Verdict: **PASS** per the binary's own documented exit-code contract
  (`main.cpp:23-25`: "0 clean run, 1 other launch failure, 2 skipped (no
  dylib), 3 recovered from the known `obscan_context_destroy()` vendor
  crash"). This is **not** a clean run: the worker dlopen'd the real vendor
  SDK, created a context, enumerated the device (`get_device_num` x7,
  `count=0` every sample despite the device being detected mid-run at
  `DeviceManager.cpp:188 Device changed! removed: 0, added: 1`), then
  created a full MoltenVK/Vulkan instance (no interposer active — this
  target never sets `DYLD_INSERT_LIBRARIES`) and **crashed with SIGSEGV**
  inside the vendor SDK during `obscan_context_destroy()`. The launcher
  caught the worker's death and exited 3, per its own design, rather than
  propagating the crash.
- Observable: `[launcher] Worker crashed inside the vendor SDK (child killed
  by signal Segmentation fault: 11). This is a known upstream issue (see
  README.md "Known issues") — the launcher process is unaffected and is
  exiting cleanly instead of propagating the crash.` / `[launcher] Exit code
  3 = recovered from known vendor crash.`

### Crash sweep (smoke_test only)

`scripts/hw-postflight.sh 2026-10-19-smoke-test '2026-10-19 16:42:02' smoke_test`:

> hw-postflight: crash report matched by app_name + timestamp.
> - Crash report: [`2026-10-19-smoke-test.crash.ips`](./2026-10-19-smoke-test.crash.ips) from `smoke_test-2026-10-19-164218.ips`, sha256 `4696bafc7f8cd18db7990f6916a14dc61c20b7a5cd49d358295f7242158b56f0` (incident `5B6E5E38-5600-41FE-BDDA-9FAB11DBDBFA`)

Not re-diagnosed against this crash report's frames beyond the above — this
matches the exact failure mode `smoke_test`'s own header comment already
names (the vendor's ncnn+OpenMP runtime crashing on a worker-spawned
background thread the first time `obscan_context_destroy()` runs), so no new
lead is being filed.

## Open questions

- `smoke_test`'s `get_device_num` reported `count=0` on all 7 samples even
  after the log shows the device was added (`DeviceManager.cpp:188`). This is
  the probe tracked by **#69** (`smoke_test`'s two gaps — the exit-2 collision
  and a delayed/repeated device-count probe, `DONE`); the 7 x 500 ms cadence
  is #69's fix, and this run is consistent with it rather than raising a new
  question.
- Is the known `obscan_context_destroy()` crash expected on every run? The
  repo's evidence says yes: **three** recorded `smoke_test` runs
  (`2026-10-18-oq7b-listenerless-smoke-test.md`, its `-rerun.md`, and this one)
  all hit the same vendor crash. Exit 3 is the normal outcome for this binary,
  not an occasional one.

## Board

- Row move: none proposed. **The survey this record first carried was wrong**
  and is corrected here: **#69** is `smoke_test`'s own gap row (`smoke_test`'s
  two gaps — the exit-2 collision and a delayed/repeated device-count probe,
  `DONE`), and **#115** lists `tools/smoke_test` in its path set. Rows
  #89/#91 cover the *features* `bounded_watchdog_test`/`tracking_status_test`
  exercise. **`smoke_test` is not a new run**: `2026-10-18-oq7b-listenerless-
  smoke-test.md` and its `-rerun.md` are prior recorded runs of the same
  binary, the rerun ending with the same `Exit code 3 = recovered from known
  vendor crash.` This batch is the first recorded run of
  `tracking_status_test`, `bounded_watchdog_test` and `mesh_tools_smoke_test`
  under their own names.
