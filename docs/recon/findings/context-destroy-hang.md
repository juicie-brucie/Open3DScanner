# `obscan_context_destroy`'s hang — the teardown path, the SuperPoint/Vulkan step, and what distinguishes the cases (row #89 recon seed)

*Static half plus the run-record cross-check. Addresses and shapes, no
decompiled output. Companion to the run record
(`tests/results/2026-10-18-plan73-quit-instrumented.md` + its correction).*

## 1. The call as the SDK runs it

`_obscan_context_destroy` @`0xb61c`, in order [inferred — static]:

1. entry log; a NULL handle returns `0x32620107` (the family's null rule);
2. `OrbbecScanContext::can_destroy_this` @`0x1e348` — the guard (§3); a
   nonzero answer is returned to the caller as-is (early-out at `0xb654`–`0xb6c8`);
3. the context's deleting destructor through `[vptr+8]`; the caller's handle
   slot is nulled (`0xb66c`);
4. success log;
5. `OBDeviceManagement::release()` @`0x5c14d8` — still the only caller of
   `release()` — the device + SDK teardown (rpc clients ×4, sensors,
   AbstractDevice, then the SDK's `Context.cpp:89 "Context destroyed"`);
6. `log_utils::log_shutdown()` @`0x164a10`;
7. **`texture_mode::shutdown_network()` @`0x8254b4` — the terminal step (§2)**;
8. returns 0.

`OBDeviceManagement::release()` @`0x5c14d8` clears the member at `+0x158` and
runs `device::OrbbecDevice::~` (D1 @`0x1b4bbc`) → the SDK device teardown (the
plan4 teardown-ports listing maps this chain; its runtime sample — the clean
acceptance run — shows the rpc-client/sensor/AbstractDevice lines, then
`Context destroyed`, then `obscan_context_destroy -> OB_SCAN_SUCCESS (0)`).

## 2. The terminal step: a construct-then-destroy of SuperPoint (ncnn + Vulkan)

`texture_mode::shutdown_network` @`0x8254b4` is the last thing the destroy
does before returning, and it does something unexpected [inferred — static]:

- it reads a once-flag; if the SuperPoint singleton has **not** been
  initialized, it **constructs it first** — `SuperPoint::C1Ev` @`0x825414`
  (under a guard, and registering the singleton with the runtime at
  `0x825518`) — and only then tail-calls `SuperPoint::shutdown()` @`0x82239c`.
  So the destroy path constructs the feature-matching network purely to shut
  it down again.
- `SuperPoint::C1Ev` @`0x825414` → `ncnn::Net::C2Ev` @`0x111e0b0` +
  `SuperPoint::InitEnv` @`0x81cdc0` → **`ncnn::create_gpu_instance(nullptr)`
  @`0x10f318c`** (called at `0x81cde0`; then `ncnn::get_gpu_count` @`0x10f56a8`
  and a "gpu available" byte at `+0x47`) → `ncnn::load_vulkan_driver`
  (called at `0x10f31fc`) → `vkGetInstanceProcAddr` (the dylib's only
  reference, `0x1129534`) → `vkCreateInstance` → MoltenVK → VkInstance +
  VkDevice.
- Linkage check: `liborbbec_scan_arm64.dylib` links
  `@loader_path/libMoltenVK.dylib`; `libOrbbecSDK_arm64.dylib` does not. The
  Vulkan creation is the scan dylib's own (ncnn), not the sensor SDK's.
- Our own interposer documents the observed outcome and is the reason it
  exists: "obscan_context_destroy() constructs Creality's SuperPoint
  feature-matching network (ncnn + Vulkan, via MoltenVK on macOS) purely to
  immediately tear it down again — it's never actually used. On this hardware
  that step has been observed to either crash fast (a known, separate libomp
  bug) or hang indefinitely right after MoltenVK creates a VkDevice"
  (`engine/src/vulkan_disable_interpose.c`, [hw-confirmed: the run record it
  cites]).

**Correction to the run record's reading**: the "ends mid-MoltenVK" tail is
not Vulkan *teardown* output — it is the *creation* banner
(`[mvk-info] MoltenVK version 1.4.1 …`, `Created VkInstance … as requested by
app …`, `Created VkDevice to run on GPU Apple M1 with the following 33 Vulkan
extensions enabled:`, then the extension list). The hang lands at/after that
creation; the file simply stops there.

## 3. The guard — the only condition the call branches on

`OrbbecScanContext::can_destroy_this` @`0x1e348` [inferred — static]:
takes a per-context byte lock at `+0x70` (a spin-lock loop), then asks the
session-side guard `bbe4e2e6::can_destroy_this` @`0x230cc`; if that refuses,
it logs and returns the refusal, and `obscan_context_destroy` returns it
directly (the `cbnz` early-out).

`bbe4e2e6::can_destroy_this` @`0x230cc` [inferred — static]:
- calls `OBScanSessionThreadManager::thread_is_idle(<name>)` @`0x719f48` with
  the 15-character thread name **"destroy session"** (the SSO string built
  from `0x42df781`); if that thread is **not idle**, it returns
  **`0x32620110` (`OB_SCAN_ERROR_MULTI_CALLED`)** (`0x231ec`–`0x231f0`);
- otherwise it logs, frees a session member, then walks
  `OBScanSessionDataManagement::get_total_base_ptr()`'s vector and asks each
  non-NULL subsystem through its vtable (`[vptr+0x18]`); the first nonzero
  answer is returned, else 0 (allowed).

**This is the `MULTI_CALLED` refusal** the wrapper's poll loops see and retry
(0–1 in the acceptance runs; plan73's `session_destroy` saw 5 refusals in
0.6 s). It is a *busy* answer from the vendor's own thread manager, not an
error — and it is the only thread-state condition inside the call.

## 4. What distinguishes the two cases (settled)

The runs differ by **whether the worker was launched through the launcher
that injects the MoltenVK blocker** [hw-confirmed: the acceptance log
`2026-10-17-scan-server-live-acceptance.log`, the plan65 rerun log, and the
plan73 app-stdout log]:

- **Launcher path — resolves.** `scan_server --live` → `RunLiveMode`
  (`tools/scan_server/main.cpp` ~1435–1438; `RunLiveMode` starts at `:1394`) sets
  `DYLD_INSERT_LIBRARIES=<libMoltenVK.dylib>:<vulkan_disable_interpose.dylib>`
  before `RunSelfAsWorker` execs the worker (`engine/src/worker_process.cpp:150`).
  The worker loads the interposer (`[vulkan_disable_interpose] loaded` —
  acceptance log line 2); `vkCreateInstance`/`vkCreateDevice` then return
  `VK_ERROR_INITIALIZATION_FAILED (-3)`, so SuperPoint's GPU init fails fast
  and `SuperPoint::shutdown()` runs on a non-GPU net: the acceptance log shows
  the blocked lines immediately before
  `obscan_context_destroy -> OB_SCAN_SUCCESS (0)` and exit 0.
- **App path — hangs.** The app spawns the worker **directly**:
  `tools/macos_app/main.mm:170` sets `arguments = @["--live-worker", port,
  library, seconds]` — the worker flag, not the launcher — and nothing else in
  the app or the worker sets `DYLD_INSERT_LIBRARIES` (the only references in
  `main.cpp` are `RunLiveMode`'s). The app-run logs carry **no launcher line
  and zero interposer lines** (plan73: 0 matches; plan65 rerun: 0 matches) →
  the SuperPoint construction runs for real → the MoltenVK banner is the last
  output (plan73 app-stdout, after `Context destroyed`) → the process spins at
  287%+ CPU and the first `obscan_context_destroy` call never returns.

So the acceptance runs' "resolved in <2 s" and the app runs' hang are the
**same call with and without the blocker**. The vendor's teardown-time GPU
construction is the mechanism; the interposer's presence is the discriminator.

## 5. Candidates: confirmed / discarded

- **Post-destroy MoltenVK/Vulkan step — confirmed, with the §2 correction**
  (a fresh *construction*, not a teardown).
- **Prior session/scan state — not the discriminator.** Both families had live
  sessions; the construct-at-teardown branch fires whenever SuperPoint is not
  already initialized, and the interposer runs completed with the same session
  shape. (The plan73 quit used the non-blocking stop and saw more guard
  refusals than the acceptance runs — a harness/timing difference; the guard's
  busy answer is §3, and the run proceeded past it into the device teardown.)
- **Device connection state — discarded.** The same device (Creality Raptor
  Pro, GVCP to `127.0.0.1`) and the same symptoms appear in both families —
  including `sendto failed with error:49` in the *clean* acceptance log
  (line 31, during bring-up) and the same GVCP rate-limited summaries after
  `Context destroyed` in both hang runs. The network state does not track the
  outcome.
- **Thread/CPU contention — a consequence, not a cause.** The 287%+ CPU is the
  spin after the VkDevice step; the call's only thread-state condition is the
  guard (§3), which resolved in every run (the SDK teardown logs prove the
  call got past it).

## 6. What genuinely needs a run

1. **The decisive A/B**: spawn the app's worker path with the interposer
   injected (or route the app through the launcher) → expect the destroy to
   return. That converts the correlation into causation (and is the direction
   the build row owns).
2. **Where the spin lives** once the GPU path runs for real: a `sample`/
   spindump of the hung worker would name the blocking frame inside
   MoltenVK/ncnn. Static evidence places it at/after VkDevice creation only.
3. **The crash-vs-hang split** (the known libomp crash vs this hang): which
   outcome the real GPU path takes, and whether it is deterministic.
4. **Whether any session shape initializes SuperPoint before teardown** (then
   the construct branch is skipped and no Vulkan is created): checkable in app
   runs by whether a MoltenVK banner ever appears before the teardown.

## OPEN QUESTIONS

- **OQ-89a** — the exact spin frame (needs §6.2's sample).
- **OQ-89b** — whether the interposer's absence is the *only* discriminator or
  the app path differs in some other way; the §6.1 A/B settles it. (The env
  injection is the only difference visible statically.)
- **OQ-89c** — what each session subsystem answers through its
  `[vptr+0x18]` check (the guard returns the first nonzero answer; only
  needed if the wrapper wants to distinguish "busy" from "broken").
- **OQ-89d** — whether `SuperPoint::shutdown()` itself can block when reached
  through the *unconstructed* branch; the run evidence places the hang before
  it (mid-banner), so statically this stays open.
