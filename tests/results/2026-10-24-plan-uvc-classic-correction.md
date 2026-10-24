# Correction — 2026-10-24-plan-uvc-classic.md (the error code and the lead)

- Date: 2026-10-24
- Corrects: `2026-10-24-plan-uvc-classic.md`, the "On failure" lead, its open questions, and its
  Board section.
- **Error code:** `0xe00002be` is **`kIOReturnNoResources`** (`IOReturn.h`:
  `iokit_common_err(0x2be)`), **not** `kIOReturnNotPrivileged` (that is `0xe00002c1`). The
  failing call is `IOCreatePlugInInterfaceForService(..., kIOUSBDeviceUserClientTypeID, ...)` —
  the classic **device** user client — on a device whose interfaces are `IOUSBHostInterface`
  nodes.
- **Lead:** the error is not evidence of a missing entitlement or privilege. The likely cause is
  the classic device user client being the wrong plug-in for the `IOUSBHostDevice` service.
  The interfaces are visible and unowned: `--diag-interfaces` finds 7 scanner interfaces, all
  `owner="(none)"`.
- **Next test:** the modern IOUSBHost path (`OPENSCANNER_UVC_IOUSBHOST=1`) on merged `main`
  (which carries #487's alt fix); the classic path as root only as a diagnostic to rule out
  privilege, not as the expected fix.
- **Board:** `#(uvc-classic)` is not a board row and `FAIL` is not a board status. The run's note
  goes on #171/#178, which stay `HW`.
- Source: `2026-10-24-plan-uvc-classic.raw.log` lines 153–156 (the capture error) and 130–148
  (the `--diag-interfaces` output); IOKit's `IOReturn.h`.
