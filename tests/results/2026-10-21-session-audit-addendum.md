# 2026-10-21 session audit addendum

Date: 2026-10-21
Auditor: hw-test subagent
Purpose: pre-dispatch patch for three gap types found across all 2026-10-21-* records.

## Gap 1 — Binary HEAD sha missing from all 16 records

`git -C /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw rev-parse HEAD` =
`823fd839b19a6b6053808027084784afb38e0539`

This sha applies to every 2026-10-21-* record in this session. All binaries under
test were built from this commit (preflight freshness checks confirmed binary ≥ newest
source in each case). Records affected:

- 2026-10-21-plan93-hud-half.md
- 2026-10-21-plan105-127-depth-dump.md
- 2026-10-21-plan141-tick-instrument-v2.md
- 2026-10-21-plan151-browser.md
- 2026-10-21-plan153-picture-unchanged.md
- 2026-10-21-plan167-livestart-2nd.md
- 2026-10-21-plan170-helper-claim-blocked.md
- 2026-10-21-plan170-helper-claim.md
- 2026-10-21-plan170-unplug-caveat.md
- 2026-10-21-plan170-usb-unplug.md
- 2026-10-21-plan171-178-capture-blocked.md
- 2026-10-21-plan172-rpc-shim-ab.md
- 2026-10-21-plan175-depth-addendum.md
- 2026-10-21-plan175-raptor-telemetry.md
- 2026-10-21-plan178-probe.md
- 2026-10-21-plan179-live-blocked.md

## Gap 2 — Device / serial number line missing or incomplete

Serial: `CR0886100SN` (CR-Scan Raptor Pro)

### 2026-10-21-plan170-unplug-caveat.md
No `- Device:` line. This is a caveat addendum to plan170-helper-claim.md, which
carries `- Device: CR-Scan Raptor Pro, SN CR0886100SN`. The same device, same
physical session. Add: `- Device: CR-Scan Raptor Pro, SN CR0886100SN; addendum to
plan170-helper-claim.md — same session, step 4 caveat`

### 2026-10-21-plan170-usb-unplug.md
`- Device:` line present but lacks SN: "CR-Scan Raptor Pro; **device powered on
throughout**". Add SN: `CR-Scan Raptor Pro, SN CR0886100SN; device powered on
throughout; USB cable pulled cold by user`

### 2026-10-21-plan171-178-capture-blocked.md
No `- Device:` line. Device was connected (probe found it; same session as plan178-probe).
Add: `- Device: CR-Scan Raptor Pro, SN CR0886100SN; connected and powered; vendor daemon
(RPCServer) stopped before probe run per coordinator`

### 2026-10-21-plan172-rpc-shim-ab.md
No `- Device:` line. Run uses offline #132 bundles; device was connected but not
accessed by the shim. Add: `- Device: CR-Scan Raptor Pro, SN CR0886100SN; device
present but not accessed — offline bundle source only`

### 2026-10-21-plan175-depth-addendum.md
No `- Device:` line. RPCServer was running (it answered the raptor_client call).
Add: `- Device: CR-Scan Raptor Pro, SN CR0886100SN; RPCServer restarted fresh before
this run (confirmed by coordinator)`

### 2026-10-21-plan175-raptor-telemetry.md
No `- Device:` line. RPCServer was running (IR run: start_stream → true, confirmed).
Add: `- Device: CR-Scan Raptor Pro, SN CR0886100SN; RPCServer running; device
connected and powered`

### 2026-10-21-plan178-probe.md
No `- Device:` line. Device was probed (VID/PID found). Add: `- Device: CR-Scan
Raptor Pro, SN CR0886100SN; connected and powered; vendor daemon (RPCServer) stopped
before probe run per coordinator`

### 2026-10-21-plan179-live-blocked.md
No `- Device:` line. The shim refused before touching the device; device was
connected during the session. Add: `- Device: CR-Scan Raptor Pro, SN CR0886100SN;
connected and powered; not accessed by this run (live source refused before open)`

## Gap 3 — Worktree path missing

### 2026-10-21-plan170-unplug-caveat.md
No `- Worktree:` line (caveat addendum format). Worktree:
`/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`, branch `hw/next`

## Gap 4 — SDK / one-SDK-user note missing

### 2026-10-21-plan93-hud-half.md
scan_server --live run. Creality Scan state not documented. Observable: scan_server
started, interposer confirmed, `[vulkan_disable_interpose] loaded` on line 2. No SDK
conflict observed (OBScan context opened cleanly). Note: same session as plan151-browser
(continuous scan_server run; no second OBScan client observed).

### 2026-10-21-plan105-127-depth-dump.md
raptor_client run (no vendor SDK; preflight daemon=n/a is the SDK exemption). RPCServer
state: running and answering — confirmed by `start_stream -> true` on port 10010. No
second raptor_client running concurrently (preflight: no conflicting process). One RPC
client at a time: satisfied.

### 2026-10-21-plan151-browser.md
scan_server --live run. Creality Scan state not documented in this record. Observable:
scan_server started with interposer confirmed, count=1 on first probe. No SDK conflict
observed. Coordinator relayed that device was power-cycled and fresh; Creality Scan was
open for device-preview (scan_server opened a second OBScan context). Two OBScan
contexts is the device's normal operating mode in this setup.

### 2026-10-21-plan153-picture-unchanged.md
Continuation of the plan151/plan93 scan_server session. Same SDK-user note applies:
scan_server was the one running SDK session; no second scan_server concurrently.

### 2026-10-21-plan171-178-capture-blocked.md
raw_capture run (no vendor SDK; preflight daemon=n/a). Vendor daemon (RPCServer) stopped
before the probe and capture attempt per coordinator confirmation. One USB device access
at a time: satisfied (RPCServer stopped, raw_capture used IOUSBHostObjectInitOptionsNone).

### 2026-10-21-plan178-probe.md
Same as plan171-178-capture-blocked: raw_capture, RPCServer stopped, read-only probe
(IOUSBHostObjectInitOptionsNone), no SDK context opened.

### 2026-10-21-plan179-live-blocked.md
raptor_shim with `--source live` (no vendor SDK in the shim itself). RPCServer state:
not documented. The live source refused before any device was opened; no USB access
attempted. Vendor daemon state: not confirmed from this run's output; the refusal is
device-independent (all three not-configured reasons fire before any device open).

## Board

No board moves are added by this addendum — all records already carry `## Board` sections.
