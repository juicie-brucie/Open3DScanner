# USB Descriptor Map — Row #169 / Phase D Seed 1

*No device attached, no USB captures, no vendor code. Documented from existing findings only. Every descriptor-level claim requires device attachment; nothing here is inferred as fact.*

## 1. What Is Known (From Existing Findings)

### 1.1 Device Identity

- **VID**: `0x2BC5` (Orbbec) — observed in `ob_get_device_enumeration_info` result (`protocol-spec.md` §1.2)
- **PID**: `0x0C03` (Creality Raptor Pro) — same source
- **Serial**: `CR0886100SN` — same source
- **USB mode**: USB 3.0 — observed in hardware log `f4c5f86` (`plan7-detection-path.md` §4)

### 1.2 The USB Ownership Model

- **RPCServer owns the device exclusively** via IOKit (`ioreg UsbExclusiveOwner = RPCServer`) — `FULL-INDEPENDENCE.md` §1
- **OBScan never touches USB itself** — it talks to RPCServer over localhost TCP/UDP
- **The SDK has two watchers**: USB (libusb hotplug) and Net (GVCP) — `plan7-detection-path.md` §2
- **Even the USB device is discovered via GVCP on loopback** (127.0.0.1) — the device answers GVCP discovery despite being physically USB
- **The SDK dedupes at DeviceManager level** — USB and network discovery converge to one device object

### 1.3 The Logical Interface Map (From RPC Methods)

The RPC method names carry a suffix `<device-id><interface-index>` (`protocol-spec.md` §1.1):

| Interface Index | Port | Stream Type | Purpose |
|-----------------|------|-------------|---------|
| 0 | 10010 | 7 (depth) | Depth/IR streaming |
| 2 | 10013 | 6 (IR) | IR streaming |
| 4 | 10012 | — | RGB streaming |
| 6 | 10011 | — | IMU streaming |

**These are logical interfaces exposed by RPCServer, not USB endpoints.** The mapping from logical interfaces to USB endpoints is not known without USB descriptor captures.

### 1.4 Phase D's Target (From FULL-INDEPENDENCE.md)

Phase D — C2: replace the daemon. Implement **UVC streaming + HID + the vendor command interface**. This implies:

- The device exposes **UVC** (USB Video Class) for streaming
- The device exposes **HID** (Human Interface Device) for controls
- The device exposes a **vendor-specific interface** for calibration/flash access

But the exact USB descriptor layout (which interfaces are UVC, which are HID, which are vendor-specific) is not documented.

## 2. What Is Not Known (Requires Device Attachment)

### 2.1 USB Descriptors

The following cannot be determined without USB descriptor captures:

- **Configuration descriptors**: how many configurations, which are active
- **Interface descriptors**: how many interfaces per configuration, their classes/subclasses/protocols
- **Endpoint descriptors**: endpoint addresses, transfer types (bulk/interrupt/isochronous), max packet sizes
- **UVC-specific descriptors**: Video Control interface, Video Streaming interfaces, Extension Units, Processing Units, Input/Output terminals
- **HID report descriptors**: report IDs, usage pages, logical/physical ranges
- **Vendor-specific descriptors**: any non-standard interface or endpoint

### 2.2 The UVC/HID/Vendor Split

Phase D's target is "UVC streaming + HID + vendor," but the split is not documented:

- Which interfaces are UVC? (presumably depth/IR/RGB, but not confirmed)
- Which interfaces are HID? (presumably IMU or control, but not confirmed)
- Which interfaces are vendor-specific? (presumably calibration/flash, but not confirmed)
- Are there multiple UVC Streaming interfaces (one per camera) or one multiplexed?
- Are the laser control properties (props 3, 152, 153, 154, 155 — `property-semantics-row101.md` §3) carried by UVC Extension Units or vendor commands?

### 2.3 The RPC-to-USB Mapping

The RPC methods (`protocol-spec.md` §1.2) are logical operations; their USB-level implementation is not known:

- `ob_rpc_get_frame` → which USB endpoint? UVC isochronous? UVC bulk? Vendor bulk?
- `ob_rpc_start_stream` / `ob_rpc_stop_stream` → UVC SET_INTERFACE? Vendor control? HID feature report?
- `ob_rpc_set_pu` / `ob_rpc_get_pu` → UVC Processing Unit control? Vendor control? HID feature report?
- `ob_rpc_send_data` (vendor commands, opcode 1–31) → vendor bulk endpoint? Vendor control transfer?
- Flash read (opcode 25) → vendor bulk? Vendor control?

## 3. Operator Requests (Requires Device Attached)

To settle the USB descriptor map, the operator must capture:

### 3.1 System-Level USB Enumeration (macOS)

```bash
# Full USB descriptor tree (includes VID/PID, interfaces, endpoints)
system_profiler SPUSBDataType > usb-system-profiler.txt

# IOKit USB device properties (includes IOKit class, driver, properties)
ioreg -p IOUSB -l -w 0 > usb-ioreg.txt

# Alternative: libusb-based enumeration (if libusb is installed)
# Requires a small tool or Python script using libusb
```

### 3.2 USB Descriptor Dumps (Device-Side)

If the operator has access to a USB protocol analyzer (e.g., Wireshark with USBPcap on Windows, or a hardware analyzer):

- **Device descriptor**: VID, PID, bcdUSB, bDeviceClass/SubClass/Protocol
- **Configuration descriptor(s)**: bNumInterfaces, wTotalLength
- **Interface descriptors**: bInterfaceNumber, bAlternateSetting, bNumEndpoints, bInterfaceClass/SubClass/Protocol
- **Endpoint descriptors**: bEndpointAddress, bmAttributes (transfer type), wMaxPacketSize, bInterval
- **UVC-specific**: VC header, input/output terminals, processing units, extension units, VS header, format descriptors, frame descriptors
- **HID report descriptor**: usage pages, report IDs, logical/physical ranges

### 3.3 Wireshark USB Capture (Windows or Linux)

On Windows (with USBPcap) or Linux (with usbmon):

```bash
# Windows: USBPcap filter on the device's VID/PID
# Linux: usbmon trace on the device's bus/devnum
# Wireshark filter: usb.idVendor == 0x2bc5 && usb.idProduct == 0x0c03
```

Capture a full session (discovery → start → stream → stop) to correlate USB transfers with RPC methods.

## 4. The Driver Model Answer (What Is Standard vs Vendor-Specific)

### 4.1 The Standard UVC/HID Model

If the device follows the standard UVC + HID model:

- **UVC Video Control (VC) interface**: device-level controls, terminal topology, processing units
- **UVC Video Streaming (VS) interface(s)**: one per camera (depth, IR, RGB), isochronous or bulk endpoints for frame data
- **UVC Extension Units (XUs)**: vendor-specific controls mapped to UVC control transfers
- **HID interface**: IMU sensor, or device-level controls (laser on/off, exposure), feature reports for property get/set
- **Vendor bulk interface**: calibration/flash access, firmware upload, diagnostic commands

### 4.2 The Vendor-Specific Model (Alternative)

If the device uses a fully vendor-specific model:

- **Vendor bulk endpoints**: all streaming and control over vendor bulk transfers
- **Vendor control transfers**: device-level commands (start/stop stream, property get/set)
- **No UVC or HID**: the device does not expose standard USB classes

### 4.3 The Hybrid Model (Most Likely)

Based on Phase D's target ("UVC streaming + HID + vendor"), the device likely uses a hybrid:

- **UVC for streaming**: depth/IR/RGB cameras exposed as UVC Video Streaming interfaces
- **HID for IMU**: IMU sensor exposed as a HID sensor device
- **Vendor for controls**: laser control, property get/set, calibration, flash access over vendor-specific interface

But the exact split is not known without USB descriptor captures.

## 5. Mapping to RPC Methods (Tentative, Requires Confirmation)

The RPC methods (`protocol-spec.md` §1.2, `property-semantics-row101.md`) can be tentatively mapped to USB-level operations, but each mapping requires USB capture confirmation:

| RPC Method | Tentative USB Mapping | Confidence |
|------------|----------------------|------------|
| `ob_rpc_get_frame` | UVC VS isochronous/bulk transfer (frame payload) | LOW |
| `ob_rpc_start_stream` | UVC SET_INTERFACE (alternate setting) or UVC SET_CUR (commit control) | LOW |
| `ob_rpc_stop_stream` | UVC SET_INTERFACE (alternate setting 0) or UVC SET_CUR (commit control) | LOW |
| `ob_rpc_start_imu_stream` / `ob_rpc_stop_imu_stream` | HID SET_REPORT (feature report) or vendor control | LOW |
| `ob_rpc_get_pu` / `ob_rpc_set_pu` | UVC PU GET_CUR / SET_CUR, or HID feature report, or vendor control | LOW |
| `ob_set_ir_frame_light_type` | UVC XU control or vendor control | LOW |
| `ob_rpc_send_data` (vendor commands) | Vendor bulk endpoint or vendor control transfer | LOW |
| Flash read (opcode 25) | Vendor bulk endpoint | LOW |
| Calibration read (opcode 3, 27, 29–31) | Vendor bulk endpoint | LOW |

**All mappings are LOW confidence** — they are educated guesses based on typical UVC/HID/vendor models, not observed facts.

## 6. Implications for C2 (Phase D)

C2 (replace the daemon) must implement:

1. **UVC streaming**: claim the UVC interfaces, set alternate settings, read isochronous/bulk endpoints, decode UVC payloads
2. **HID controls**: claim the HID interface, send/receive feature reports for IMU and device controls
3. **Vendor commands**: claim the vendor interface, send/receive bulk transfers for calibration, flash access, laser control, property get/set
4. **The RPC layer**: expose the same TCP RPC interface that RPCServer currently provides (ports 10010/10011/10012/10013)

But the exact USB descriptor layout is required to implement C2. Without USB descriptors, C2 cannot:

- Know which interfaces to claim
- Know which endpoints to read/write
- Know the transfer types (isochronous vs bulk)
- Know the max packet sizes
- Know the UVC terminal topology
- Know the HID report format

## 7. Summary

| Question | Answer | Confidence |
|----------|--------|------------|
| Is the device USB? | **Yes** (VID 0x2BC5, PID 0x0C03, USB 3.0) | HIGH |
| Who owns the USB device? | **RPCServer** (IOKit exclusive owner) | HIGH |
| How many logical interfaces? | **4** (depth, IR, RGB, IMU) | HIGH |
| What are the USB interfaces? | **Not known** (requires USB descriptor captures) | — |
| What are the USB endpoints? | **Not known** (requires USB descriptor captures) | — |
| What is standard UVC? | **Not known** (requires USB descriptor captures) | — |
| What is standard HID? | **Not known** (requires USB descriptor captures) | — |
| What is vendor-specific? | **Not known** (requires USB descriptor captures) | — |
| What does Phase D target? | **UVC streaming + HID + vendor** (implies hybrid model) | MEDIUM |

## 8. Open Questions

- **OQ-169a**: What are the USB configuration/interface/endpoint descriptors? Requires `system_profiler SPUSBDataType` or USB protocol analyzer capture.
- **OQ-169b**: Which interfaces are UVC, which are HID, which are vendor-specific? Requires USB descriptor dump.
- **OQ-169c**: What are the UVC Extension Units (if any)? Requires UVC descriptor dump.
- **OQ-169d**: What is the HID report descriptor (if any)? Requires HID descriptor dump.
- **OQ-169e**: Which USB endpoints carry which RPC methods? Requires USB capture correlated with RPC capture.
- **OQ-169f**: Are the laser control properties (props 3, 152, 153, 154, 155) carried by UVC XUs, HID feature reports, or vendor commands? Requires USB capture.

---

**Wall**: `docs/recon/**`
**Status**: `[inferred]` — documented from existing findings only, no USB descriptors read, no device attached.
**Operator action required**: USB descriptor capture (see §3).
