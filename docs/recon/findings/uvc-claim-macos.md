# Claiming the Raptor UVC interfaces on macOS — ownership & the vendor's claim recipe

*Recon spec for the IOKit/UVC-ownership question (Task 1 / the `--probe` sees-7
but `--capture` cannot open interface 1 puzzle). Static analysis of the
installed vendor binary + no-root host observation (ioreg / running processes /
unified log / loopback capture). No device test run, no sudo executed; the
deliverable is a spec, not code lifted from the binary.*

**Status**: `[inferred, host-observed 2026-10-24]` — host/wire observation, not a
gated `tests/results` run. Ownership verdict SETTLED; the vendor's exact
interface-seize call is OPEN (needs a thinned dylib slice or a root trace).
**Wall**: `docs/recon/**`.
**Related**: `privilege-model.md` (#177) — device-level open & the
root-vs-entitlement helper model; `usb-descriptor-map.md`, `enumeration-state.md`
— the interface inventory; `frame-codec.md` — stream endpoints.

Device: **Creality Raptor Pro**, USB `0x2bc5/0x0c03` (Orbbec VID), serial
`CR0886100SN`, `bDeviceClass=239` (IAD composite), 1 configuration, 7 interfaces.

---

## 1. The question

Why does `--probe` see seven interfaces but `--capture` cannot `Open` interface
1, and is the vendor's success **root** or an **entitlement**?

## 2. Verdict

**It is root, not an entitlement — and the interfaces `--capture` wants are
already matched by Apple's in-kernel UVC driver.** `--probe` only reads
descriptors (no `Open`), so it always works; `--capture` calls `USBInterfaceOpen`
on a class-14 video interface that the OS has already bound to its UVC driver,
so the open is refused with `kIOReturnExclusiveAccess`. The vendor daemon wins
by running as **root** and seizing the interface from that driver.

## 3. Evidence

### 3.1 Static (vendor binary, clean-room)

- **RPCServer runs as root.** `/Library/LaunchAgents/com.creality.RPCServer.plist`
  execs `/usr/bin/sudo /Library/Creality/RPCServer` (`KeepAlive`): a user
  LaunchAgent that escalates to root via `sudo` (consistent with #177 §4).
- **No special entitlement.** `codesign -d --entitlements -` on RPCServer
  returns none — no `com.apple.vm.device-access`, no DriverKit transport
  entitlement. Only a Developer ID Application signature (CREALITY 3D (HK)
  TECHNOLOGY LIMITED, TeamID `DMR5SZUGP9`) + hardened runtime (`flags 0x10000`).
  So the vendor's root-vs-entitlement answer is **root**.
- **Classic IOKit, not libusb.** `otool -L RPCServer` links `IOKit.framework`
  + `@loader_path/libOrbbecSDK.1.7.dylib`; **no** libusb, **no**
  `IOUSBHost.framework`. The USB claim lives in `libOrbbecSDK.1.7.1.dylib`.

### 3.2 Host observation (no root; device attached)

- **All 7 interface nubs exist** as `IOUSBHostInterface`, registered + matched —
  this **overturns the earlier premise** "ioreg shows no child
  `IOUSBHostInterface` services":

  | nub | role | bInterfaceClass | iface# |
  |-----|------|-----------------|--------|
  | @0,@1 | Depth Camera | 14 (UVC video) | 0,1 |
  | @2,@3 | IR Camera | 14 (UVC video) | 2,3 |
  | @4,@5 | RGB Camera | 14 (UVC video) | 4,5 |
  | @6 | IMU | 3 (HID) | 6 |

- **The six video interfaces are matched by Apple's in-kernel UVC driver.** Each
  class-14 nub carries `CameraAssistantConfiguration = {"UVCCameraHideControls"
  …}` and is `matched`; `UVCAssistant` (CoreMediaIO system extension, pid 603)
  and `appleh13camerad` are the always-on camera daemons. A plain userspace
  `USBInterfaceOpen` on such an interface is refused with
  `kIOReturnExclusiveAccess` because it already has a matched driver — **not**
  because another app holds it.
- **The IMU (HID) is owned by DriverKit** (`AppleUserUSBHostHIDDevice`).

### 3.3 Verified-idle re-sample (2026-10-24 ~08:01) + a contamination correction

An earlier host pass overlapped a **WT-HW hardware test**: the unified log shows
`IOUSBHostPipe::abortGated … Creality Raptor Pro … endpoint 0x81: aborting 98`,
`0x82: aborting 100`, `0x85: …` at **07:45–07:55** — active streaming pipes
being torn down. Re-sampled once truly idle (**RPCServer down, no
hw-test/SDK/WT-HW process, no Raptor log events after 07:55:45, and a 5 s
`tshark` on `lo0` tcp/10010 captured 0 packets**):

- the 7-nub / UVC-match **structure is unchanged** (not a test artefact), but
- **no interface has an opener at rest** — the device subtree shows no
  `IOUserClientCreator` on any nub. (The macOS camera stack opens a video
  interface only while a capture session is live; the Raptor appears in every
  camera app's device list but is not held open at rest. An earlier draft citing
  `AVCaptureDALDevice` holders was reading a *removal* event at 01:58:31, not a
  steady-state claim.)
- **Bonus lead (tentative, test-teardown origin):** the streaming endpoints seen
  aborting are **0x81, 0x82, 0x85** (bulk/interrupt IN) — candidate depth / IR /
  RGB stream endpoints; confirm against `frame-codec.md`.

## 4. The recipe — "to claim these UVC interfaces on macOS, do X"

1. **Run as root.** This is the lever the vendor uses; no entitlement
   substitutes for it in the vendor build.
2. **Do not plain-`Open` the video interfaces — seize them.** Use
   `USBInterfaceOpenSeize` (classic IOUSBLib) to take the interface from the
   kernel UVC driver, or open with `IOUSBHostObjectInitOptionsDeviceCapture` /
   `…DeviceSeize` (the OS "capture this camera away from the camera assistant"
   path). **Which one the vendor uses is the open item** (§6).
3. **Contend with the kernel UVC driver, not a peer app.** At rest no process
   holds the interfaces; the blocker is the in-kernel UVC/CameraAssistant match
   (hence `kIOReturnExclusiveAccess`). A capture session must seize from that
   driver. A camera app opening the Raptor as a webcam would additionally
   contend, but that is transient, not the resting state.
4. The **IMU** interface is HID/DriverKit — a separate owner from the video path.

## 5. Reconciliation with the helper privilege model (#177)

`privilege-model.md` argues a user-scope agent with `com.apple.security.device.usb`
can do an IOKit USB **device** exclusive open without root, and flags UVC/HID
interface access as deferred to Phase D (§3.3). This finding is that deferred
piece and sharpens it: the contended resource is the **class-14 interface**,
already bound to Apple's kernel UVC driver, which is a stronger claim than the
bare device open #177 tested. So #177's "no root needed" result (for the
`IOUSBHostDevice` open) does **not** automatically extend to the UVC interface
open — the vendor does the interface claim as **root + seize**. Whether a
signed user-scope agent with the camera/USB entitlements can seize the UVC
interface **without** root is the key thing #177's four-step test should now be
extended to prove at the *interface* (not just device) level.

## 6. OPEN QUESTIONS

- **OQ-UVC1 — the exact interface claim call + ordering.**
  `USBInterfaceOpenSeize` vs `IOUSBHostObjectInitOptionsDeviceCapture`/`…Seize`,
  and the order relative to device `Open` / `SetConfiguration`. Lives in
  `libOrbbecSDK.1.7.1.dylib`. Needs either a **thinned arm64 slice** for REA (its
  Ghidra provider rejects the universal Mach-O; Hopper/IDA absent; the guard hook
  blocks `lipo`-ing the vendor dylib by name — operator provides the slice), or a
  **root dtrace** of RPCServer's IOKit user-client calls during a claim — blocked
  on this host (SIP enabled + hardened-runtime target; would need SIP off).

- **OQ-UVC2 — can a signed user-scope agent seize the UVC interface without
  root?** Extend #177's four-step test from the device open to the class-14
  interface seize, with `com.apple.security.device.usb` (+ camera) entitlements.

- **OQ-UVC3 — the USB-bus claim sequence.** A Wireshark USB-bus capture during a
  real claim would show the `SET_CONFIGURATION` / `SET_INTERFACE`(alt) control
  transfers. macOS exposes no USB capture interface to `tshark` without root
  setup, and RPCServer must be triggered to claim — operator-gated. (The loopback
  RPC channel *is* observable without root and is silent at rest.)

- **OQ-UVC4 — stream endpoint map.** Confirm 0x81 / 0x82 / 0x85 (seen aborting
  under test teardown) against the depth / IR / RGB streams in `frame-codec.md`.
