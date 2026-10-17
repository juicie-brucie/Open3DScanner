# Hardware test — PLAN #8/#10: device_message_listener_test

**Date:** 2026-10-17
**Worktree/branch:** `Open3DScanner-wt-build1` / `build/plan8-interpose` (PR #34)
**Target program:** `tests/hardware/device_message_listener_test`
**Command:**
```
cmake --build build --target device_message_listener_test
./build/tests/hardware/device_message_listener_test/device_message_listener_test
```
**Device:** Creality Raptor Pro (CR0886100SN), USB3.0, connected and powered
**Exit code:** `1`
**Verdict:** **All seven explicit checks PASSED.** Exit code 1 is from an
unrelated **segfault during teardown**, after every assertion had already
succeeded — not a failed check, and not the 180s timeout (the whole run took
~4 seconds). Raw output: `2026-10-17-plan8-device-message-listener.raw.log`
(same directory).

## Interposer check

The task asked to confirm the **first** line is `[launcher] DYLD_INSERT_LIBRARIES=
…libMoltenVK.dylib:…libvulkan_disable_interpose.dylib`. It is present with the
correct content —

```
[launcher] DYLD_INSERT_LIBRARIES=/Applications/CrealityScan.app/Contents/PlugIns/libMoltenVK.dylib:/Users/brucemackay/DevReposiMac/Open3DScanner-wt-build1/build/engine/libvulkan_disable_interpose.dylib
```

— pointing at this worktree's own freshly-built interpose dylib, so this was
unambiguously the right binary with the fix active. It is **not literally the
first printed line**, though: it's the launcher's own `printf`, made before the
`fork`/`exec`, but the launcher never `fflush`es it, so it sits in the parent's
stdio buffer while the worker child (which `fflush`es after every line) gets
its own output onto the shared stream first — the line lands near the *end* of
the capture (right before the crash report) purely from buffer-flush ordering
between the two processes, not because a different binary ran. Noted literally
per the instructions rather than silently waved off; worth an `fflush(stdout)`
right after that `printf` in the launcher if this check needs to be reliable
line-order in future runs, but that's a one-line source change and out of
scope here ("do not modify any source file").

## The seven checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `add(NULL handle, listener)` == `OB_SCAN_ERROR_DEADLINE_EXCEEDED` (845283591) | **PASS** |
| 2 | `add(handle, NULL listener)` == `OB_SCAN_ERROR_INVALID_ARGUMENT` (845283584) | **PASS** |
| 3 | `add(handle, listener_a)` #1 == `OB_SCAN_SUCCESS` | **PASS** |
| 4 | Double add (`listener_b`, no remove) == `OB_SCAN_ERROR_INVALID_ARGUMENT` (§2.3 single-slot claim) | **PASS** |
| 5 | `remove(handle, listener_a)` == `OB_SCAN_SUCCESS` | **PASS** |
| 6 | `remove(NULL handle, listener)` == `OB_SCAN_ERROR_DEADLINE_EXCEEDED` | **PASS** |
| 7 | `add(handle, listener_a)` after remove == `OB_SCAN_SUCCESS` (OQ-3: remove *does* clear the slot) | **PASS** |

`[worker] DONE (failures=...)` never printed — the process crashed after check
7 but before the summary line, so that count is inferred from the per-check
`[PASS]`/`[FAIL]` lines actually printed, all seven of which read `[PASS]`.

## OQ-4 — thread identity (resolved)

```
[worker]   messages delivered during add: 1; callback thread id hash=17304887900616587565; registration thread id hash=17304887900616587565
```

**The callback thread hash and the registration (calling) thread hash are
identical.** Combined with the already-established synchronous-delivery
finding (PLAN #1), this fully resolves OQ-4: the device-message callback on
registration fires **on the same thread that called
`add_device_msg_listener`**, not a separate vendor-internal thread — at least
for this specific delivery (the synchronous one fired during registration
itself; steady-state delivery during later `CONNECT`/`DISCONNECT` events from
the polling/GVCP watcher thread was not separately re-checked here since only
one `DEVICE_CONNECT` arrived, at `16:54:49`, and no thread-hash was logged for
it — only the registration-time message's thread was captured by the test's
`g_callback_thread`/`g_registration_thread` comparison).

## The crash

Full tail of the log:

```
[worker][PASS] add after remove == SUCCESS (OQ-3 cleared slot)
[10/17 16:54:49.652777][warning][...][ObException.cpp:5] Trying to create a device that you've already created! SN/ID: CR0886100SN, uid: 30:30:34:30:35:30
[10/17 16:54:49.737223][info][...] ... GVCPClient.cpp:421 ...
[10/17 16:54:49.737229][info][...] ... GVCPClient.cpp:457 ...
[launcher] DYLD_INSERT_LIBRARIES=.../libMoltenVK.dylib:.../libvulkan_disable_interpose.dylib
worker did not exit cleanly: child killed by signal Segmentation fault: 11
```

No `obscan_context_destroy()` report line and no `DONE` line were ever
printed, so the crash happened somewhere in the test's unlogged final
sequence: the cleanup `remove_fn(dm, &g_listener_a)` call (`main.cpp:178`,
removing a listener that check 7 just successfully *re-added* — this remove
has no `Check()`/print around it) immediately followed by
`obscan_context_destroy()` (`main.cpp:184`). Can't yet tell which of those two
calls actually faulted from this log alone.

One real candidate lead, visible right above the crash: `ObException.cpp:5`
logs **"Trying to create a device that you've already created!"** for the
same SN immediately after check 7's `add` succeeded — i.e. re-registering the
listener after remove appears to have triggered the vendor SDK to attempt
*re-creating* the device object, colliding with one that still exists from the
first registration. Worth keeping as a possible contributing trigger, but it
is very likely **not the primary cause** — see below.

### This almost certainly isn't a new bug — link to prior work

This project already has a fully root-caused, **never-fixed** `obscan_context_
destroy()` crash on record, documented in `README.md` ("Known issues",
`obscan_context_destroy() crashes inside the vendor SDK on macOS`): the call
triggers `texture_mode::shutdown_network()`, which constructs Creality's
SuperPoint AI network (ncnn + Vulkan) purely to immediately tear it down —
the first OpenMP parallel region ever run in a minimal host process, and the
vendor's bundled `libomp.dylib` 5.0 segfaults on a NULL `__kmp_threads`
global during the join barrier, confirmed via lldb, reproduced 100% of the
time **on vendor-spawned worker threads**, not the calling thread. Every
direct fix was ruled out at the time (`NSApplicationLoad`,
`OMP_NUM_THREADS=1`, `KMP_DUPLICATE_LIB_OK=TRUE`, pre-warming `libomp`,
skipping the call entirely — which just trades it for a different crash, an
unjoined background thread aborting on exit). The only real mitigation on
record is exactly the one already in use here: running the risky call inside
a disposable `RunSelfAsWorker` child process (`engine/include/openscanner/
worker_process.h`) so a crash there only kills the worker, which is precisely
what happened — the launcher survived and reported the signal cleanly,
exactly as designed.

This is a **different failure mode from the Vulkan/SuperPoint *hang*** that
`vulkan_disable_interpose.c` fixes (see `docs/research/live-scanning-
pipeline-overview.md` and the README's "False start #1/#2/#3" history for
that saga) — the interposer blocks `vkCreateInstance`/`vkCreateDevice` to
stop ncnn's Vulkan device construction from hanging, but `shutdown_network()`
still runs its OpenMP-parallel region regardless of whether Vulkan succeeds
or is blocked, so the interposer being active here doesn't rule this out —
if anything, it makes the libomp explanation *more* likely, since the known
Vulkan hang is accounted for and this crashed fast instead of hanging, which
is the libomp crash's own documented signature. The "device already created"
warning may simply be incidental noise from the same teardown window, not a
distinct root cause — plausible, but secondary to the much better-evidenced
libomp explanation already on record.

## Recommendation for `/openscanner/reconcile`

- All of PLAN #8's and PLAN #10's own items (§2.3, OQ-3, §5 both error paths,
  OQ-4) are now hardware-confirmed. Good to move those to `DONE`.
- The teardown `SIGSEGV` is very likely the same already-documented,
  never-fixed `libomp`/OpenMP crash in `obscan_context_destroy()` →
  `texture_mode::shutdown_network()` recorded in `README.md`'s "Known
  issues" — not a new bug, and not something the Vulkan interposer was ever
  going to fix (it addresses a hang, not this crash). Worth a PLAN item that
  cross-references that README section and `logs/stop_abi_fix_validation/`
  rather than being investigated as something new; the "device already
  created" warning logged just before it is a secondary, not-yet-ruled-out
  lead. Not investigated further here per "do not modify any source file" —
  left for recon.
