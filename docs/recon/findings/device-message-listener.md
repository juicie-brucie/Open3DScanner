# Device-message listener registration — PLAN #1

**Target:** `liborbbec_scan.3.5.3.dylib`, arm64 slice
(sha256 `184d92a87bdebba1cb5207ee1a111d49aa497f46088cc9f12d9abd3b59c017ec`),
_matching the `__TEXT` of the shipped fat binary; `liborbbec_scan.dylib` and
`liborbbec_scan.3.5.0.dylib` in the bundle are symlinks to it._

**Status:** `SPEC-READY`

**Method:** static. Complete external symbol table (`nm -gU`, 25,366 symbols),
targeted arm64 disassembly of the listener entry points and their implementation
chain, plus decode of the vendor's `*_name` lookup tables. There was **no dynamic
tracing in this pass**, so every statement below is a static-binary fact; anything
that is a reading of intent rather than of code is tagged **[inferred]**.

This closes the "undocumented; a listener is what starts the watcher" open item
in `docs/recomp/PLAN.md` — but see §3 and OQ-7, where it also **narrows an
earlier claim**.

---

## 1. The entry points

Both C entry points exist (exported, unmangled) and are thin forwarders:

| C symbol | native target | shape |
| --- | --- | --- |
| `obscan_device_management_add_device_msg_listener` | `OBDeviceManagement::add_device_msg_listener(OBScanDeviceMessageListener_*)` | `(handle, listener)` → status |
| `obscan_device_management_remove_device_msg_listener` | `OBDeviceManagement::remove_device_msg_listener(OBScanDeviceMessageListener_ const*)` | `(handle, listener)` → status |
| `obscan_device_management_add_device_tips_msg_listener` | `OBDeviceManagement::add_device_tips_msg_listener(OBScanDeviceTipsMessageListener_*)` | same |
| `obscan_device_management_add_control_msg_listener` | `OBDeviceManagement::add_control_msg_listener(OBScanControlMessageListener_*)` | same |
| `obscan_context_add_data_msg_listener` | `OrbbecScanContext::add_data_msg_listener(OBScanDataMessageListener_*)` | `(context, listener)` → status |
| `obscan_context_add_process_msg_listener` | `OrbbecScanContext::add_process_msg_listener(OBScanProcessMessageListener_*)` | same |

The struct pointer argument is a pointer to the **listener object**, and the
handle is passed straight through as the native `this`. There is no copy of the
listener and no handle indirection layer.

## 2. Registration semantics

`OBDeviceManagement::add_device_msg_listener` validates in this order, and all
three failure paths share the same return value `845283584`
(`OB_SCAN_ERROR_INVALID_ARGUMENT`):

1. **Handle is NULL** — rejected in the C wrapper, before the native call;
   returns `845283591` (§5).
2. **`listener == NULL`** — native log: `the listener is nullptr !`.
3. **A listener is already installed** — native log:
   `OBScanDeviceMessageListener is already set !`. The probe is the
   `std::function` manager word at `this+0xb8`, i.e. an earlier registration
   that was never removed. **Registration is single-slot: a second add without a
   remove fails.**
4. Otherwise it installs and returns `0` after logging
   `device state callback success !`.

**Installation** does two things:

- Stores a `std::function<void(OBScanDeviceMessage_)>` in the manager at
  `this+0xa0`. Its target captures `{monostub, <caller's listener pointer>}` —
  the listener object is referenced, never copied.
- If the device object at `this+0x158` (`device::OrbbecDevice`) exists, it also
  calls `device::OrbbecDevice::set_call_back(std::function<void(bool,
  device::DeviceConnectStatus, std::list<device::DeviceInfo>)>)`, which forwards
  to `device::OrbbecInternalDevice::set_call_back`. That home in turn calls
  `OrbbecInternalDevice::deviceConnectedCall(shared_ptr<ob::DeviceList>)`
  **immediately**.

The last step is the mechanism behind the previously-observed *synchronous*
DISCONNECT delivered inside the add call: registering the connect callback makes
the internal device re-run its device-connected handler against the *current*
device list, and with no device attached that yields a DISCONNECT message before
`add_device_msg_listener` returns. **[inferred from the call that immediately
follows registration, not traced dynamically]**

## 3. What creates the device, and what really starts the watcher

Correcting an implication in the current header:

- `OBDeviceManagement::init_device(OBScanContextConfig_ const*)` builds
  `device::OrbbecDevice` and stores it at `this+0x158`. It is called from
  `ObscanContextBase`'s **constructor** — i.e. the device object already exists
  after `obscan_context_create`, before any listener is registered.
- This dylib imports **no IOKit and no CFRunLoop symbols at all** (`nm -u`).
  The IOKit matching-notification watcher therefore lives in the separate
  Orbbec SDK dylib this one links against, not here.
- So the earlier statement "the watcher thread is only started as a side effect
  of registering a real listener" is **consistent with** the Frida observation
  (zero IOKit registrations until a listener exists) but should be re-worded:
  the watcher is not *implemented* here, and what registration does here is
  attach a connect callback, which triggers an immediate device-list evaluation.
  Which side actually performs the first IOKit registration — **resolved** in
  [`plan7-detection-path.md`](plan7-detection-path.md) (the SDK dylib's embedded
  libusb darwin backend, on `UsbContext` construction). The watcher start itself
  is *not* tied to listener registration: both device watchers are constructed
  inside `obscan_context_create` [hw-confirmed: f4c5f86].

## 4. Delivery path and the "lost if no listener" rule

`OBDeviceManagement::update_camera_status(bool, device::DeviceConnectStatus,
std::list<device::DeviceInfo>)` is the emitter: it builds `OBScanDeviceMessage_`
values and calls `OBDeviceManagement::send_device_msg(OBScanDeviceMessage_)` at
four sites. `send_device_msg`:

1. returns immediately when no listener is installed (`this+0xb8 == 0`) —
   **a message emitted before registration is dropped, not queued**;
2. formats `send device msg id : <NAME>` via `OBScanDeviceMessageID_name(msg_id)`;
3. invokes the installed `std::function` with the message.

If the `std::function` is present but empty, control reaches
`std::__1::__throw_bad_function_call`, so a thrown C++ exception can escape a
listener-registered-but-cleared state. Delivery happens on the emitting
(vendor) thread, so a listener must be thread-safe. **[thread identity inferred]**

Note the same rule holds for the context-level families: data/process
listeners exist to receive frames/process events, and preview refuses without
them.

## 5. Error codes

- **Generic null-handle code.** Every `obscan_device_management_*` wrapper (all
  24 of them) and both context-listener wrappers return exactly
  `845283591` (`0x32620107`) when their primary handle is NULL:
  `OB_SCAN_ERROR_DEADLINE_EXCEEDED`. This is a shared precondition code, **not**
  a timeout. Callers must not treat `OB_SCAN_ERROR_DEADLINE_EXCEEDED` as
  necessarily meaning "timed out"; a NULL handle produces it too.
- **Registration guards** return `845283584`
  (`OB_SCAN_ERROR_INVALID_ARGUMENT`) for both the NULL-listener and
  already-registered cases.
- **Success** is `0`.

`OBScanStatus` is now **fully verified** against the vendor's own
`OBScanStatus_name` jump table: 42 named values plus 26 genuinely reserved slots
(`0x3262010a–0x0f`, `0x3262011a–0x20`, `0x3262012a–0x2f`, `0x32620139–0x3f`).
Every value already in `obscan_api.h` matches the native table exactly — the
enum needs no correction, only the note that the reserved holes are real.

## 6. Struct layout (natively verified this pass)

`OBScanDeviceMessage_` — **16 bytes, verified**:

| offset | type | field |
| --- | --- | --- |
| `0x00` | `int32` | `msg_id` |
| `0x04` | `int32` | padding |
| `0x08` | `void*` | `data` |

Evidence: the value is passed by value in `x1` (low 8 bytes) / `x2` (the `data`
pointer) at every call site, and is materialised with a single 16-byte store.
This confirms the header's offsets rather than merely assuming them.

`OBScanDeviceMessageListener_` is still taken to be a lone delegate pointer at
`0x00`. It was **not** re-verified in this pass (the capture stores
`{monostub, listener}`, and following the monostub's vtable was out of scope) —
see **OQ-2**.

## 7. Device-message vocabulary — complete

Recovered by decoding `OBScanDeviceMessageID_name`. All twelve values of the
device family:

| value | hex | name |
| --- | --- | --- |
| `0` | `0x00000000` | `OB_SCAN_MESSAGE_ID_DEVICE_CONNECT` |
| `308412672` | `0x12620100` | `OB_SCAN_MESSAGE_ID_DEVICE_DISCONNECT` |
| `308412688` | `0x12620110` | `OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_SUCCESS` |
| `308412689` | `0x12620111` | `OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_FAILED` |
| `308412690` | `0x12620112` | `OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_REBOOT_FAILED` |
| `308412691` | `0x12620113` | `OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_FAILED_IMAGE_PROBLEM` |
| `308412692` | `0x12620114` | `OB_SCAN_MESSAGE_ID_DEVICE_CONNECT_NEED_CALIBRATION` |
| `308412693` | `0x12620115` | `OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_REBOOT_SUCCESS` |
| `308412694` | `0x12620116` | `OB_SCAN_MESSAGE_ID_DEVICE_CONNECT_WITH_PROBLEM` |
| `308412695` | `0x12620117` | `OB_SCAN_MESSAGE_ID_DEVICE_CONNECT_READ_PARAMS_FAILED` |
| `308412696` | `0x12620118` | `OB_SCAN_MESSAGE_ID_DEVICE_CONNECT_CRC_CHECK_FAILED` |
| `308412697` | `0x12620119` | `OB_SCAN_MESSAGE_ID_DEVICE_UPGRADE_FAILED_LOW_VERSION` |

Only the first two were known before; the connect-failure variants are directly
actionable (they explain a device that enumerates but cannot be used).

Encoding: the family prefix is `0x1262`, with `0x100` selecting the device
family and the low byte the member index. The data family starts at
`0x13620100` and process at `0x14620100`, which matches the values already in
the header. **[pattern inferred from three families]**

## 8. Lifetime rules

- The **listener object is caller-owned** and must outlive registration; the SDK
  retains a pointer to it inside the stored `std::function`.
- Release with `obscan_device_management_remove_device_msg_listener` before
  destroying the listener. Registration is single-slot (§2.3), so an add/add
  pair fails rather than replacing.
- Messages emitted before registration are **dropped**; there is no queue.
- Delivery is on the emitting thread.

## 9. Symbols observed without any reference

`OBDeviceManagement::bind_device_connect_callback()`,
`OBDeviceManagement::send_device_msg(int)` and
`OBDeviceManagement::init_device_msg(int)` have no direct `BL`/`B` caller and no
`ADRP`+`ADD` address reference. Detection covered only those two forms, so
"unused" is **not proven**; they read as remnants of an older wiring where the
connect callback was bound separately (rather than from
`add_device_msg_listener`) and messages were sent by id.

---

## OPEN QUESTIONS

- **OQ-1** — `data` payload semantics. `send_device_msg(int)` supplies
  `data = this+0x167` (non-NULL, an interior pointer to the manager) for
  `msg_id == 0` and NULL for every other id, but `update_camera_status` emits
  through the struct overload with its own `data` values. Per-call-site payloads
  are not yet mapped. Nothing in the header should claim `data` is a device list
  until this is settled.
- **OQ-2** — Is `OBScanDeviceMessageListener_` really a single pointer at
  `0x00`? Verifiable by following the monostub vtable at `__DATA_CONST`
  `0x454d3b0`. The other listener families use the same shape.
- **OQ-3** — Does `remove_device_msg_listener` fully clear the `std::function`
  (making a subsequent add succeed)? Not disassembled.
- **OQ-4** — Exact thread that invokes the listener (vendor device thread?), and
  whether any re-entrancy is possible from inside the callback. Needs dynamic
  tracing.
- **OQ-5** — The `tips` / `control` / `data` / `process` / `calib` families use
  the same `*_name` jump-table shape and are decodable by the same method; not
  decoded in this pass. Worth doing for PLAN #2 and PLAN #3.
- **OQ-6** — Is the opaque `ObScanDeviceManagementHandle` literally an
  `OBDeviceManagement*`? The wrapper passes it straight through as `this`, and
  the member offsets used (`0xb8`, `0x158`, `0xa0`) match that object, but the
  vendor type name is `OBScanDeviceManagementHandle_`. Low risk for callers, who
  only forward the out-parameter.
- **OQ-7** — *Resolved* → [`plan7-detection-path.md`](plan7-detection-path.md).
  Watchers (libusb USB hotplug + GVCP net) start at `obscan_context_create`
  inside `libOrbbecSDK.1.7.1.dylib`; the first `IOServiceAddMatchingNotification`
  belongs to that dylib's embedded libusb, triggered by `UsbContext`
  construction. A listener-less with-device run is still unobserved (OQ-7b
  there).

## Evidence

Raw listings for this pass (disassembly, symbol tables, decoded tables) are in
`.recon-cache/listings/` — `OBDeviceManagement.region.asm`,
`obscan_dm_add_device_msg_listener.asm`,
`OBDeviceManagement_add_device_msg_listener.asm`,
`OBDeviceManagement_send_device_msg_msg.asm`,
`OrbbecInternalDevice_set_call_back.asm`, `classes.symbols.txt` — and
`.recon-cache/listings/liborbbec_scan_arm64.symbols.txt`. Per the wall in
`docs/recon/README.md` these are read-only for `builder` and are not reproduced
here.
