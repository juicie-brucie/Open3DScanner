# Hardware test — PLAN #8 (`device_message_listener_test`) — BLOCKED, not run

**Date:** 2026-10-16
**Target program:** `tests/hardware/device_message_listener_test/main.cpp`
**Result:** **Not run.** Inspected, found a safety gap, stopped before touching
hardware.

## What's missing

`device_message_listener_test`'s `CMakeLists.txt` links against
`vulkan_disable_interpose` and defines `VULKAN_DISABLE_INTERPOSE_DYLIB`, the same
way `tools/scan_server` and `tools/live_capture_test` do — but `main.cpp`'s
launcher never actually calls `setenv("DYLD_INSERT_LIBRARIES", ...)` with it before
re-execing into the worker. Both of those other tools do this explicitly; this one
doesn't.

That matters specifically because this test calls `obscan_context_destroy()` at
the end (`main.cpp:184`) with no scan ever started — i.e. it *will* reach the exact
call this project has already confirmed (twice, earlier the same day) hangs on
this hardware without the interpose fix, building Creality's SuperPoint/ncnn
network via MoltenVK for no reason. Without `DYLD_INSERT_LIBRARIES` set, that call
would run unprotected.

The launcher's own `RunSelfAsWorker(..., /*timeout_seconds=*/60)` would eventually
`SIGKILL` a worker stuck there — but killing a worker *while it's inside the
vendor's own context_destroy teardown* is a materially different (and worse) risk
than killing one before teardown starts: it's exactly the failure mode that has
left this hardware needing a physical power cycle before. A 60s timeout is also
shorter than the ~180s budget this project's other tools use for exactly this
reason.

## What would fix it

Mirror `tools/scan_server/main.cpp`'s or `tools/live_capture_test/main.cpp`'s
launcher: before `RunSelfAsWorker(...)`, derive `libMoltenVK.dylib`'s path as a
sibling of the resolved library path and `setenv("DYLD_INSERT_LIBRARIES",
moltenvk_path + ":" + VULKAN_DISABLE_INTERPOSE_DYLIB, 1)`. Both existing call
sites can be copied near-verbatim (macOS-only, `#if defined(__APPLE__) &&
defined(VULKAN_DISABLE_INTERPOSE_DYLIB)`).

## Recommendation for `/openscanner/reconcile`

This isn't a spec-vs-hardware mismatch — it's a gap in the test program itself
(builder's `tests/hardware/*`, not `docs/recon/**`). PLAN #8 shouldn't move past
`BUILD` until this is fixed; re-run once it is.
