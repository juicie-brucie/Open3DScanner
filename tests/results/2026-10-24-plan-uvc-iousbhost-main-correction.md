# Correction — 2026-10-24-plan-uvc-iousbhost-main.md (the failure stage)

- Date: 2026-10-24
- Corrects: `2026-10-24-plan-uvc-iousbhost-main.md`, the "On failure or block" lead and the
  first open question.
- **The error is the interface lookup, not the seize.** `tools/raw_capture/iokit_source.mm`'s
  `UsbInterfaceHandle::Open` emits `interface N is not on the device` only when
  `FindInterfaceService(spec.interface_number)` returns 0 (15 attempts); a failed
  `IOUSBHostInterface initWithIOService:options:IOUSBHostObjectInitOptionsDeviceSeize` has a
  different message: `interface N could not be seized from its current owner: <error>`. The
  record's raw shows the former, so the seize code never ran.
- The bare `--diag-interfaces` (same process, same run) sees **7 scanner interfaces**, all
  owned by `pid 603, UVCAssistant`; `--probe` sees the three video streams. The filtered
  `FindInterfaceService` — `IOServiceMatching(kIOUSBHostInterfaceClassName)` plus
  `kUSBVendorID`/`kUSBProductID`/`kUSBInterfaceNumber` — returns 0. The blocker is therefore the
  **matching filter**, not raw visibility and not (yet) the UVCAssistant ownership.
- The next fix: make the interface lookup use the same in-process enumeration the diag proves
  works (iterate `IOUSBHostInterface` nodes and compare their registry properties), or correct
  the filtered match; then the `DeviceSeize` path can run. The classic path's
  `IOCreatePlugInInterfaceForService` failure (`kIOReturnNoResources`, #496) is a separate dead
  end; do not spend the next run on it.
- Source: `2026-10-24-plan-uvc-iousbhost-main.raw.log` (the diag lines and the capture error);
  `tools/raw_capture/iokit_source.mm` (the retry block); #496's correction addendum for the
  classic path.
