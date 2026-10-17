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

The interposer is still required regardless: without `DYLD_INSERT_LIBRARIES`,
dyld aborts with "symbol not found in flat namespace" when loading the worker,
because `vulkan_disable_interpose` needs MoltenVK's real `vkCreateInstance`/
`vkCreateDevice` symbols to be present before it binds. So the missing
`setenv` is a genuine launcher gap, not cosmetic.

The remaining blocker is the timeout, not the Vulkan path. The SuperPoint/ncnn
hang seen during `obscan_context_destroy()` is a second, Vulkan-unrelated hang
(see `logs/stop_abi_fix_validation/README.md`); those runs never entered the
Vulkan path. The interposer does not prevent that hang. The launcher's
`RunSelfAsWorker(..., /*timeout_seconds=*/60)` would `SIGKILL` a worker stuck
there, and killing a worker *while it is inside the vendor's own
context_destroy teardown* is a materially different (and worse) risk than
killing one before teardown starts — it is exactly the failure mode that has
left this hardware needing a physical power cycle before. A 60s budget is also
shorter than the ~180s this project's other tools use for exactly this reason.

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
`BUILD` until this is fixed; re-run once it is. (PLAN #10 tracks the timeout
raise to 180 s alongside this interposer fix.)
