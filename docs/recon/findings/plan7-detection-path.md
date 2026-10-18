# The macOS device detection path — PLAN #7

**Targets:**
- `libOrbbecSDK.1.7.1.dylib`, arm64 slice, sha256
  `5134a211296484473eaa7e4801788943a6ccb6b4a8660465b0a2c042d9dd925b`
  (thin slice regenerated from the shipped fat binary in
  `.recon-cache/vendor/` — a new X-Ghidra snapshot was imported and persisted).
- its consumer `liborbbec_scan.3.5.3.dylib` arm64 (sha
  `184d92a87bdebba1cb5207ee1a111d49aa497f46088cc9f12d9abd3b59c017ec`) for the
  trigger link into `obscan_context_create`.

**Status:** `SPEC-READY`

**Method:** existing hardware run `f4c5f86` (see
`tests/results/2026-10-16-plan1-device-message-listener.md` and its `.raw.log`),
plus one fresh static pass over the SDK slice (symbol tables, REA/Ghidra
disassembly of the watcher functions and xrefs between them). Every runtime
behavior is tagged; static-only claims are tagged `[inferred]`.

This resolves **OQ-7** ("which side performs the first
`IOServiceAddMatchingNotification`?") and rewords the detection-paragraph claims
in `obscan_api.h`. The device-*message* ABI itself (PLAN #1,
`device-message-listener.md`) is **unchanged** by this file: registration
semantics, single-slot rule, error codes, and message delivery are as specified
there. Only *when*, *where*, and *how* the SDK detects hardware devices is
corrected here.

---

## 1. Resolution of the header conflict

Three earlier claims are on the table:

1. *"Registration starts the device watcher"* (header, pre-PLAN #1) —
   **contradicted** by hardware: the watcher machinery starts inside
   `obscan_context_create`, before any listener exists.
   [hw-confirmed: f4c5f86 — the whole startup block prints on the
   `obscan_context_create` calling thread (14044955 in the log):
   `MacPal.cpp:159 Create PollingDeviceWatcher!`, then
   `DeviceManager.cpp:15 Current found device(s): (0)`, then
   `Context.cpp:82 EnumerateNetDevice:true`]
2. *"liborbbec_scan contains no IOKit; the watcher lives in the Orbbec SDK
   dylib"* — confirmed and sharpened: the SDK dylib not only *contains* the
   watcher symbols, it contains **the entire detection stack**, including an
   embedded libusb whose darwin backend performs the IOKit matching
   registrations. [confirmed: `strings`/`nm` — `MacPal.cpp`,
   `DeviceManager.cpp` (libobsensor `src/core/`) exist only in
   libOrbbecSDK; `liborbbec_scan` has zero occurrences of either]
3. *"Zero IOKit registrations until a listener exists"* (Frida,
   pre-worker-process harness) — **superseded as a causal claim.** The timing
   correlation was real but the mechanism was wrong: the first
   `IOServiceAddMatchingNotification` lands when the first
   `libobsensor::pal::UsbContext` is constructed, which in a run with a device
   attached follows discovery by ~1 s (see §4), i.e. after listener
   registration in the observed harnesses — but it is *not* the listener that
   triggers it. A listener-less context with an attached device would still
   materialize USB. [flow-chain static; conditional never positively
   observed without a listener — see OQ-7b]

## 2. What actually runs on macOS — two watchers, both inside the SDK dylib

`obscan_context_create` → `ob::Context` constructor (`Context.cpp:67/72/82`)
→ **`core::DeviceManager`** → the `pal::ObPal` interface (implemented by
`pal::MacPal`, vtable `__ZTVN11libobsensor3pal6MacPalE` @ `0x732ba0`):

| factory | addr | product |
| --- | --- | --- |
| `MacPal::createUsbDeviceWatcher()` | `0x67af8`–`0x67c3b` | `pal::LibusbDeviceWatcher`, prints `Create PollingDeviceWatcher!` (MacPal.cpp:159) while constructing it [hw-confirmed: f4c5f86; class identity is static] |
| `MacPal::createNetDeviceWatcher()` | ~`0x69c2c` | `pal::NetDeviceWatcher` — GVCP broadcast discovery, owns its thread |

Both are created **inside `obscan_context_create`**, on the caller's thread;
neither waits for listener registration.

### 2.1 USB watcher — `pal::LibusbDeviceWatcher`

- Constructed via `__shared_ptr_emplace` (control vtable `0x7330170`, object
  0x58 bytes); abstract base `pal::DeviceWatcher`; primary vtable
  `__ZTVN11libobsensor3pal19LibusbDeviceWatcherE` @ `0x7331c0`, whose **slot 0
  is `start`** (`0x6b478`).
- `start(std::function)` registers **four libusb hotplug callbacks** via
  `libusb_hotplug_register_callback` (`0x558f50`): events
  ARRIVED(1)/LEFT(2) × vendor IDs **`0x2BC5` (Orbbec)** and **`0x1B20`**
  (second watched VID, owner unidentified — [inferred] legacy vendor line);
  product −1, device class −1, user data = the watcher. The arrival/removal
  callbacks are `pal::deviceArrivalCallback` `0x68e0c` /
  `pal::deviceRemovedCallback` `0x68f94` (MacPal.cpp:264/273 logs), which run
  `pal::parseDevicePath` (`0x6890c`) and forward add/remove events into the
  DeviceWatcher → DeviceManager pipeline that produces device messages.
- Event delivery terminates in `OBDeviceManagement::update_camera_status(...)`
  (`device-message-listener.md` §4) — same path, same listener.

### 2.2 Net watcher — `pal::NetDeviceWatcher`

- `NetDeviceWatcher::start(std::function<bool(OBDeviceChangedType, string)>)`
  @ `0x97c94` spawns its own thread; polls on ~1 s cycles: first hit in the
  hardware log lands **1.009 s** after context creation
  (`Ethernet.cpp:69 NetDeviceWatcher detected new device,
  uid:30:30:34:30:35:30`). [hw-confirmed: f4c5f86; also seen in every earlier
  log]
- Notable: **even the physically-USB Raptor Pro is discovered by this path** —
  the device answers GVCP discovery on loopback (`127.0.0.1`), connection type
  still reported `USB3.0`. The SDK dedupes at DeviceManager level
  (`Device changed! removed: 0, added: 1`). [hw-confirmed: f4c5f86]

## 3. Where the IOKit calls actually live

`libOrbbecSDK` embeds a full **libusb** (local `t` symbols: `libusb_init`
`0x5561a0`, `libusb_get_device_list` `0x5544b4`, …). Its darwin backend:

- `_darwin_init` (`0x55bd70`–`0x55c027`) spawns
  `_darwin_event_thread_main` (`0x55ed88`–`0x55f007`), which issues the
  process's `IOServiceAddMatchingNotification` calls (**both** call sites in
  this slice: `0x55ee78`, `0x55eeac`) plus `_CFRunLoop*` plumbing.
- The trigger is `libobsensor::pal::UsbContext::UsbContext`
  (`0x6e080`–`0x6e1d7`): its body calls `libusb_init` (site `0x6e0c8`) **and**
  immediately `startEventHandler()` (site `0x6e1b8` → `0x6e208`, which spawns a
  `std::thread` for libusb event handling). Constructing a `UsbContext` is
  therefore the whole IOKit story. The second (and, statically, only other)
  SDK-side initiator is the firmware-upgrade path `scsi::Upgrader::Upgrader`
  (`0x93b7c`). [call chain static]

**Answer to OQ-7 as originally framed:** the first
`IOServiceAddMatchingNotification` is performed by the SDK dylib's *own
embedded libusb*, on `UsbContext` construction — not by the app, not by a
macOS system watcher, not by listener registration. Discovery of scan devices
runs in parallel over libusb hotplug + GVCP and both feed the same
DeviceManager. [chain static; ordering vs. discovery in time Open Question —
OQ-7b]

## 4. Timeline vs. the earlier Frida claim

Hypothetical-order reconciliation (all points individually evidenced):

| t | event | evidence |
| --- | --- | --- |
| 0 | `obscan_context_create` returns SUCCESS; Context/DeviceManager built; both watchers constructed | log f4c5f86 (same thread, same instant) |
| ≈1 s | GVCP poll discovers device; `DEVICE_CONNECT` delivered | log f4c5f86 (`NetDeviceWatcher detected`) |
| listener add | `add_device_msg_listener` installs function + immediate device-list re-evaluation | device-message-listener.md §2 |
| device materialized (USB3.0 mode) | `UsbDeviceLibusb` (ctor `0x6ef2c`, takes `shared_ptr<UsbContext>`) → `UsbContext::UsbContext` → `libusb_init` → IOKit matching registrations | chain static |

The earlier harnesses likely attributed IOKit activity to the listener because
the listener was registered before the first device materialization. Under
this model, IOKit registration is a *transport-init* effect (USB device
object created / upgrade path) and would also fire without any listener. This
has not been observed directly (`f4c5f86` had no Frida) — **OQ-7b.**

## 5. ABI impact

- None on the C ABI. All of this lives behind `obscan_context_create`
  (watcher start) and the device-message listener (delivery). The header's
  detection paragraphs are updated to the §1/§2 model; no signature changes.
- `obscan_device_management_add_device_msg_listener` remains the single
  supported way to receive detection results. Dropping the listener only
  disables *message delivery* — the watchers keep running for the context's
  lifetime. [inferred: watcher stop never observed in the scan pipeline]

## 6. Evidence

- `.recon-cache/libOrbbecSDK_arm64.dylib` (thin arm64 slice, sha
  `5134a211296484473eaa7e4801788943a6ccb6b4a8660465b0a2c042d9dd925b`) +
  REA/Ghidra session outputs referenced in this file (`xrefs`,
  `read_function_instructions`, `search_strings`). Raw listings live in the
  tool-output logs; per `docs/recon/README.md` they are not reproduced here.
- `tests/results/2026-10-16-plan1-device-message-listener.raw.log` (f4c5f86):
  startup block, net-watcher first hit at +1.009 s, USB3.0 device discovered
  via GVCP-on-loopback.
- `nm` symbol tables of both dylibs in `.recon-cache/listings/`
  (`liborbbec_scan_arm64.symbols.txt`) and `nm` output quoted inline above.

## OPEN QUESTIONS

- **OQ-7b** — Confirm by instrumentation that a listener-less context with an
  attached device still constructs `UsbContext` (i.e. IOKit registrations are
  purely transport-side). `f4c5f86` had no Frida; the prior "zero before
  listener" trace predates the worker harness. Task candidate for a future HW
  row.
- **OQ-7c** — Which USB vendor ID owns `0x1B20` (watched alongside Orbbec's
  `0x2BC5`)? Static-only; possibly a legacy pre-Orbbec sensor line.
- **OQ-7d** — *Closed (row #68)*. The two scheduler-style arguments around
  `libusb_hotplug_register_callback` calls (arg layout, whether an additional
  vendor-side poll thread ever exists) were read at call-granularity; the
  timer/poll mechanic naming ("PollingDeviceWatcher" vs. libusb-hotplug) is a
  vendor naming inconsistency, not a second mechanism. The exact event-pump
  thread config belongs to OQ-4 — which is **not** open: OQ-4 is *Resolved
  (row #52), one hardware confirmation left* (`device-message-listener.md`
  §4's trace: a `[cb]` line while a device-changed pass fires with the
  listener installed). See the closure note below.

## OQ-7d — closed (row #68)

`OQ-7d` (ROADMAP rev 12: "only its parent question, `#52`") is **closed**:
`#52` is delivered and landed — `ef14646` (the invocation threads and the
re-entrancy finding) and its follow-up `ad90ef6` (the invoke is
unsynchronised) — and OQ-7d carried nothing else. The only residue anywhere
on that line is OQ-4's single hardware observation (a `[cb]` line while a
device-changed pass fires with the listener installed; the trace spec is in
device-message-listener.md §4) — a specified one-observation trace, not an
open question.
