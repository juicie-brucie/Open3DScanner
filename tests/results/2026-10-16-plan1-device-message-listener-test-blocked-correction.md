# Correction — `2026-10-16-plan1-device-message-listener-test-blocked.md`

**Date:** 2026-10-17
**Corrects:** [`2026-10-16-plan1-device-message-listener-test-blocked.md`](./2026-10-16-plan1-device-message-listener-test-blocked.md)
**Raised by:** PLAN #10 (build), after verifier review of PR #34.

The blocked doc is the hardware agent's as-observed record (`c95926e`) and is
kept **unedited**. The corrections PLAN #10 asked for live here instead, because
`tests/hardware/README.md:10` says results are "one file per run, **never edited
in place**" — the first attempt at this correction rewrote the body of the
original and had to be reverted to honor that.

## What the original gets wrong

It attributes the danger of a missing `setenv("DYLD_INSERT_LIBRARIES", …)` to the
hang seen during `obscan_context_destroy()`, on the grounds that the call
"builds Creality's SuperPoint/ncnn network via MoltenVK for no reason".

`logs/stop_abi_fix_validation/README.md` records that hang as "a second,
Vulkan-**unrelated** hang": those runs never entered the Vulkan path, and the
interposer does not prevent it. So the interposer is not what stands between this
test and that hang.

## What is actually true

- The missing `setenv` is still a **genuine launcher gap** — for the dyld reason,
  and the direction of that reason matters. Inserting the interposer **without**
  MoltenVK ahead of it makes dyld abort with "symbol not found in flat
  namespace", because `vulkan_disable_interpose` binds MoltenVK's real
  `vkCreateInstance`/`vkCreateDevice` symbols at load. Setting *nothing* inserts
  nothing, so nothing binds and nothing aborts.
  `live_capture_test/main.cpp:667-677` documents exactly this ordering and sets
  both entries; this test's launcher set neither.
- The hang the original was worried about is real, but it is the
  **Vulkan-unrelated** one. A longer worker timeout is the mitigation this
  project already uses for it (`live_capture_test/main.cpp:700-708` records
  180 s as the budget "for exactly this reason"), not the interposer.
- The original's *risk* argument stands on its own and is unaffected:
  `SIGKILL`ing a worker *inside* the vendor's `obscan_context_destroy()` teardown
  is materially worse than killing one before teardown starts, and a 60 s budget
  is shorter than this project's ~180 s norm.

## Status

Both halves of PLAN #10 are done on `build/plan8-interpose`: the launcher sets
`DYLD_INSERT_LIBRARIES` with MoltenVK ahead of the interposer
(`device_message_listener_test/main.cpp:216-224`), and the worker timeout is
180 s (`main.cpp:232`).
