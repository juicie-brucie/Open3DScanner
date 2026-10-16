# Branch claude/clever-albattani-cebx0e, re-tested after their fixes — both confirmed

Follow-up to `logs/branch_claude-clever-albattani-cebx0e/` (the first
hardware test, which found a reproducible segfault). That session kept
working and pushed fixes for both the segfault and — more
significantly — a real correction to this project's own understanding
of the `obscan_context_destroy()` hang.

## Fix #1: the segfault

Commit `8d165e1` ("drop the get_device_info call that segfaulted on
hardware") removed the call that crashed in the first test. Confirmed:
**two consecutive hardware runs, zero segfaults.**

## Fix #2: the real cause of the `obscan_context_destroy()` hang — and this project's own mistake

This project's `logs/stop_abi_fix_validation/README.md` concluded the
~180s `context_destroy` hang was "Vulkan-unrelated," based on finding
zero `VK_KHR`/`VK_EXT` lines in the **native vendor scan log**. That
conclusion was wrong: the native log never contains MoltenVK or ncnn's
own output in the first place, so its absence there proved nothing.
Checking this project's own full captured process log instead (not
just the vendor log) shows MoltenVK *was* being invoked all along —
easy to verify directly: `grep -n "MoltenVK\|VK_KHR"
logs/stop_abi_fix_validation/live_capture_test_run2_180s_timeout.log`.

The other session caught this by checking the right log, then found
the actual reason `vulkan_disable_interpose.c`'s original
`vkCreateInstance`/`vkCreateDevice` interposition never fired: dyld's
`__interpose` only rewrites references dyld itself binds at load time,
but ncnn (the library Creality's SuperPoint network runs on) resolves
Vulkan entry points at runtime via `dlsym()` +
`vkGetInstanceProcAddr()` — a lookup path the original interposer
never touched. Commit `347fcac` adds interposition of `dlsym()` and
`vkGetInstanceProcAddr()` themselves, so any route to
`vkCreateInstance`/`vkCreateDevice` gets caught, however ncnn finds it.

## Hardware result: confirmed, twice, back-to-back, no power cycle

```
[vulkan_disable_interpose] intercepted vkGetInstanceProcAddr lookup of a create function
[vulkan_disable_interpose] blocked vkCreateInstance
vkCreateInstance failed -3
[worker] obscan_context_destroy took 729ms  (run 1) / 695ms (run 2)
[worker] obscan_context_destroy                        -> OB_SCAN_SUCCESS (0)
[launcher] Worker exited normally with code 0.
```

**First ever clean exit (code 0) from this project's `live_capture_test`
— twice in a row, with no power cycle between runs**, where every
previous run (with or without this project's own, incomplete Vulkan
fix) either crashed or had to be force-killed by the worker timeout.
Both runs also produced real mesh data via
`obscan_scan_get_realtime_model_mesh` (21,672 and 46,218 vertices —
the second exceeds this project's own prior best of 42,224 from
`logs/exposure_sweep_new_setup_20261016/`), and `obscan_context_destroy`
completed in under a second both times instead of hanging for 15+
minutes or exceeding a 180s timeout.

- `live_capture_test_run1_CLEAN_EXIT_0.log` /
  `live_capture_test_run2_CLEAN_EXIT_0.log` — this project's own
  worker/launcher stdout for both runs.
- `native_scan_log_run1.txt` / `native_scan_log_run2.txt` — the
  vendor SDK's own scan logs for the same two runs.

## Conclusion

Both bugs found in the first hardware test of this branch are
genuinely fixed, confirmed on real hardware, reproducibly. This also
means this project's own earlier "Vulkan fix confirmed" (then
retracted) and "new, Vulkan-unrelated silent hang" conclusions were
both based on checking the wrong log file — worth remembering before
declaring a native-SDK interaction "unrelated to X" based only on the
vendor's own logger, which doesn't capture everything running in the
process.
