# PLAN #181 — helper claim + capture (does the device-level claim change the ioreg owner?) — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raw_capture/raw_capture  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 06:19:44 >= newest source descriptor_probe.cpp 06:06:04
- Interposer: expect first line: [launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib
- Run start: 2026-10-24 06:19:48

## Preconditions

- Vendor daemon: not running (`pgrep -ifl "orbbec|obscan|UVCAssistant|scan_server"` found no matching process before the agent install; note `UVCAssistant` itself is a running system service seen later via `ioreg`, not matched by that pre-check's own name pattern).
- Device: per the operator's instruction, power-cycled before this run (not independently verified by this agent — no power-cycle control is in scope).

## Commands

```
tools/macos_helper/install_user_agent.sh install
tools/macos_helper/install_user_agent.sh status
build/tools/raptor_shim/raptor_shim --helper-status
ioreg -p IOService -n "Creality Raptor Pro" -l -r | grep -E 'UsbExclusiveOwner|bInterfaceNumber'

OPENSCANNER_UVC_IOUSBHOST=1 build/tools/raw_capture/raw_capture \
    --capture --stream depth --sink /tmp/uvc-depth-helper.rcap --max-frames 30 --max-seconds 10

tools/macos_helper/install_user_agent.sh uninstall
```

(`install_user_agent.sh` is at `tools/macos_helper/` in this tree, not `build/tools/macos_helper/` as given — no build-tree copy of the script exists; the built binary it installs, `openscanner-helper`, is under `build/tools/macos_helper/`.)

## Result

- Exit code: `2` (raw_capture); `0` for every other command above
- Verdict: **FAIL**
- Raw log: `2026-10-24-plan181-helper-claim-capture.raw.log` (same directory)
- Interposer confirmation: n/a — `raw_capture` does not create an OBScan context (stated in preflight's own output); not applicable to this target

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | helper agent installs and the launchd job reaches `state = running` | **[PASS]** `install_user_agent: registered org.openscanner.helper in gui/501`; status shows `state = running`, `pid = 48100` |
| 2 | `raptor_shim --helper-status` reports the helper's claim | **[PASS]** `[shim] helper status on org.openscanner.helper: claimed-by-helper` |
| 3 | ioreg shows the helper as device-level `UsbExclusiveOwner` while claimed | **[PASS]** `"UsbExclusiveOwner" = "pid 48100, openscanner-help"` on the top-level device node |
| 4 | ioreg per-interface owner (`bInterfaceNumber` 0–5, the UVC VS interfaces) changes from `UVCAssistant` while the helper holds the claim | **[FAIL]** all six remain `"UsbExclusiveOwner" = "pid 603, UVCAssistant"`, unchanged from before the helper claim |
| 5 | `raw_capture --capture --stream depth` opens the device and records frames while the helper holds its claim | **[FAIL]** `[capture] stop: source-error` / `[capture] detail: the USB device could not be opened: Failed to create IOUSBHostObject. (stop the vendor daemon: it holds the device)`; `frames: 0 (ir 0, depth 0, rgb 0), 0 record bytes`; exit `2` |
| 6 | helper agent uninstalls cleanly and the device-level owner line disappears | **[PASS]** `install_user_agent: removed org.openscanner.helper from gui/501 and ...plist`; the post-uninstall `ioreg` dump no longer shows the `pid 48100` owner line |

## Crash sweep

hw-postflight: no crash report for 'raw_capture' at/after 2026-10-24 06:19:48 — safe to record 'no crash report (checked DiagnosticReports for raw_capture at/after the run start)'.

## On failure or block — observable + one lead

- Observable: with the helper claimed (device-level `UsbExclusiveOwner` = helper, per-interface owner unchanged = `UVCAssistant`), `raw_capture --capture --stream depth` exited `2`: `the USB device could not be opened: Failed to create IOUSBHostObject. (stop the vendor daemon: it holds the device)`. 0 frames captured.
- Lead: likely in whatever the helper's claim actually calls (the device-level IOKit user-client open vs the per-interface UVC stream-interface claim) — the helper's device-level claim does not touch the six VS/HID interface-level owners that `IOUSBHostObject`'s open path checks, so `raw_capture`'s own open still contends with `UVCAssistant` at the interface level, not with the helper.

## Open questions

- Is the per-run `raw_capture` process meant to inherit the helper's open `IOUSBHostObject`/interface handles via IPC, rather than opening the device itself — i.e. is a separate `raw_capture` process the wrong shape, and must the capture instead run inside the helper's own process (or be driven by it over XPC)?
- Does the helper's claim need to walk down to the per-interface level (claiming interfaces 0–5, not just the top-level device node) for a separate capture process to succeed, or is interface-level claiming inherently exclusive to one process (the helper) and this card's premise (helper claims, a separate process captures) therefore not viable at all?
- Is `UVCAssistant`'s standing per-interface ownership itself something the operator/helper can release (distinct from "stop the vendor daemon", which is a different process than `UVCAssistant`), or is it a macOS system service that is never released while the device is attached?

## Board

- Row move: #181 `HW` → stays `HW`, outcome 2 from the card is confirmed (owner stays `UVCAssistant` at the interface level; the capture fails at device open). Per the card's own framing this surfaces a **#198-shaped code question** (does the capture need to run inside the helper's process, or must the helper claim the interfaces themselves) — a code decision for the dispatcher/builder, not another `raw_capture` run from this agent.
