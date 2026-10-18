# Device-message listener registration — PLAN #1

**Target:** `liborbbec_scan.3.5.3.dylib`, arm64 slice
(sha256 `184d92a87bdebba1cb5207ee1a111d49aa497f46088cc9f12d9abd3b59c017ec`),
_matching the `__TEXT` of the shipped fat binary; `liborbbec_scan.dylib` and
`liborbbec_scan.3.5.0.dylib` in the bundle are symlinks to it._

**Status:** `SPEC-READY`

**Method:** static. Complete external symbol table (`nm -gU`, 25,366 symbols),
targeted arm64 disassembly of the listener entry points and their implementation
chain, plus decode of the vendor's `*_name` lookup tables. There was **no dynamic
tracing in the original pass**, so its statements are static-binary facts;
hardware results retrieved since are cited per run (`[hw-confirmed: …]` /
`[hw-observed: …]` / `[hw-contradicted: …]`); anything that is a reading of
intent rather than of code is tagged **[inferred]**.

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
   `std::function`'s `__f_` word at `this+0xb8`, i.e. an earlier registration
   that was never removed. **Registration is single-slot: a second add without a
   remove fails.**
4. Otherwise it installs and returns `0` after logging
   `device state callback success !`.

**Installation** does two things:

- Stores an inline `std::function<void(OBScanDeviceMessage_)>` in the manager at
  `this+0xa0` (the probe word at `this+0xb8` is that function's `__f_`). Its
  16-byte capture is `{<caller's listener pointer>, the manager this}` — the
  listener object is referenced, never copied. The functor's vtable is
  `__DATA_CONST 0x454d3b0`; resolving what it does with the capture is OQ-2,
  resolved in §6.
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

**Remove→add re-enters device creation** [hw-observed:
2026-10-17-plan8-device-message-listener] — recorded here because it
qualifies the single-slot rule above. Check 7's remove→add pair drove
`PioneerDevice.cpp:54 Pioneer device created!` → `PioneerDevice.cpp:59
Pioneer device destroyed!` → `Pioneer device created!` (16:54:49.07 / .27 /
.59, all on the caller thread `14660842`), and 60 ms after
`DeviceManager.cpp:157 Device created successfully!` the NetDeviceWatcher
(GVCP) thread (`14660873`) logged `ObException.cpp:5 Trying to create a device
that you've already created! SN/ID: CR0886100SN…`. A remove→add therefore
re-enters SDK device creation while a device object from the previous round
may still be live; every step returns 0, so at the ABI level it is legal.
This is the event that ends in the §9 NetDeviceWatcher fault (NULL in
`get_active_device()`) — whether the churn *causes* that state or merely
*exposes* it is open → **#11**.

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
(vendor) thread, so a listener must be thread-safe.
**[thread identity hw-observed: 2026-10-17-plan8-device-message-listener]** —
the DEVICE_CONNECT the harness received was invoked on the worker's main
thread `14660842`, the same thread that registered the listener
(raw.log:26 prints callback-thread hash == registration-thread hash for
the add-time delivery, and check 7's CONNECT at raw.log:75 carries the
same hash). The vendor's NetDeviceWatcher (GVCP) thread `14660873` runs
the same emitter for its device-changed passes but never reached
`send_device_msg` in that run (its invocation faulted first, §9). So every
listener invocation recorded in this run — the add-time DISCONNECT
(raw.log:23) and the CONNECT (:75) — ran on the worker thread `14660842`,
and an invocation from the NetDeviceWatcher thread was never exercised.
See OQ-4.

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

`OBScanDeviceMessageListener_` is **a single delegate pointer at `0x00`,
natively resolved in the 2026-10-17-plan9 pass** (§6.1) — the header's one-word
shape is exact.

### 6.1 OQ-2 resolved — the `0x454d3b0` vtable chain

OQ-2 asked whether the listener is a lone pointer at `0x00`, verifiable by
following the "monostub vtable" at `__DATA_CONST 0x454d3b0`. Raw listing:
`.recon-cache/listings/device-msg-listener.monostub-vtable.plan9.asm`. The
address-map is the identity (`otool -l`: every segment's `vmaddr == fileoff`),
so the file offset equals the VM address.

**What `0x454d3b0` actually is.** Not a listener stub: it is the `__func`
vtable of the `std::function` installed at registration —
`std::__1::__function::__func<OBDeviceManagement::add_device_msg_listener(OBScanDeviceMessageListener_*)::$_0, void(OBScanDeviceMessage_)>`,
pinned by the typeinfo name string at `__TEXT 0x2cefbe4` referenced from the
vtable's RTTI slot. §2's earlier "{monostub, listener}" capture phrasing was a
misreading of the inline representation: the inline buffer is
`{vptr = 0x454d3b0, capture0 = listener, capture1 = manager this}` with `__f_`
pointing at the buffer (`0x5c1f90`–`0x5c1fb8`: `stp {0x454d3b0, x1}, [sp,#8]`,
`stp {x0, &capture}, [sp,#0x18]`, then `__value_func::swap` into `this+0xa0`;
`sizeof(__func) == 0x18`, per the 24-byte allocation in `__clone`).

**Slot map** (vtable-relative, each entry pinned by its symbol):

| slot | symbol (at `0x5c2194`+) | body |
| --- | --- | --- |
| `+0x00` | `~__func()` (D1) | `ret` |
| `+0x08` | `~__func()` (D0) | tail `b operator delete` |
| `+0x10` | `__clone() const` | alloc 0x18, re-store vptr, copy 16-byte capture |
| `+0x18` | `__clone(__base*)` | copy 16-byte capture only |
| `+0x20` | `destroy()` | `ret` — **nothing to clean up** |
| `+0x28` | `destroy_deallocate()` | `operator delete` path |
| `+0x30` | `operator()(OBScanDeviceMessage_&&)` | **the invoke** |
| `+0x38` | `target(type_info)` | RTTI compare |
| `+0x40` | `target_type()` | returns typeinfo id |

**The invoke** — reached from `send_device_msg` (`0x5c5420`: `ldr x0,
[x20,#0xb8]` = `__f_`, `ldr x8,[x8,#0x30]`, `add x1, sp, #0x8` = `&msg` copy,
`blr`):

```
__func::operator() @ 0x5c2204
  ldp x8, x1, [x1]    ; x8 = msg.words[0] = {int32 msg_id, int32 pad}, x1 = msg.words[1] = data
  ldr x9, [x0, #0x8]  ; x9 = capture0 = the registered listener pointer
  ldr x2, [x9]        ; x2 = [listener + 0x00]  <- THE ONLY LOAD THROUGH THE LISTENER
  mov x0, x8
  br  x2              ; tail-call delegate(x0 = msg.words[0], x1 = msg.words[1])
```

The listener object is double-dereferenced exactly once: one 8-byte load at
offset `0x00`. `__clone`/`__clone(__base*)` copy the *capture* (the 16 bytes at
`__func+8`), never walking into the listener; `destroy()` is `ret`; the invoke
never loads beyond `[listener+0]` and never writes it. So the **delivery path**
consumes one pointer-sized word of the listener object and nothing else — no
vtable, no extra field, no object header behind it (this is not an IL2CPP
object shape; word 0 is called as code). Scoped to delivery deliberately:
remove is a separate entry point and is accounted for separately — it never
dereferences the listener at all (it clears `__f_` and destroys the stored
function; OQ-3, answered).

**Delegate argument ABI.** The call is the by-value ABI of the header's
`void (*)(ObScanDeviceMessage msg)`: a static one-argument callee takes the
16-byte composite in `x0`/`x1`. The `pad` word is *not* observable as a separate
scalar: a 3-scalar `(msg_id, pad, data)` ABI would need `data` to travel in
`x2`, but in the invoke `x2` is the *code pointer* — loaded by `ldr x2,[x9]`
and consumed by `br x2` — not a `data` argument. The header signature therefore
distinguishes correctly from `(int32, int32, void*)`.
`[hw-confirmed: 2026-10-17-plan8-device-message-listener]` — the harness
log (raw.log:23 / raw.log:75) records `DEVICE_DISCONNECT (msg_id=308412672)
data=0x0` and `DEVICE_CONNECT (msg_id=0) data=0x104d93e67`, exactly what the
by-value-in-`(x0,x1)` delivery produces through this invoke for the harness's
declared `void OnDeviceMessage(ObScanDeviceMessage msg)`. Note the register
split is *emitter-side vs delegate-side*: `send_device_msg` itself receives the
message in `x1`/`x2` (its own `this` in `x0`), the delegate in `x0`/`x1` — no
contradiction, just different function signatures.

**Family cross-check.** The structurally identical `__func` vtables of the
other registered families pin the same shape:

| family | capture (`__func` vtable) | `operator()` | invoke body |
| --- | --- | --- | --- |
| device (`this+0xa0`) | `0x454d3b0` | `0x5c2204` | `ldp; ldr x9,[x0,#8]; ldr x2,[x9]; br` |
| data (context) | `0x4539308` | `0x1b7ec` | byte-identical |
| process (context) | `0x4539398` | `0x1bbc4` | byte-identical |
| tips | `0x454d440` (reg `0x5c24d4`) | `0x5c2678` | byte-identical |
| control | `0x454d560` (reg `0x5c2bdc`) | `0x5c2d78` | byte-identical |

All five families are now **static-verified at the invoke**: the tips/control
`+0x30` slots (`0x5c2678` / `0x5c2d78`) were ranged in the follow-up pass and
both bodies are byte-identical to the device family's; the `target(type_info)`
symbols following each invoke pin the two vtables as the tips/control listener
`__func`s. Raw:
`.recon-cache/listings/device-msg-listener.tips-control-invokes.plan18.asm`.
Also unrelated: the `0x454d888` capture seen next to `add_device_msg_listener`
belongs to `device::OrbbecDevice::set_call_back`'s connect lambda, not to any
listener family.

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
  retains a pointer to it inside the stored `std::function`. That requirement
  does not end at `obscan_context_destroy()`: the SDK's background threads
  (hotplug / GVCP watchers, RPC clients) can outlive destroy while still
  holding the captured pointer **[inferred: project threading model —
  `engine/src/obscan_loader.cpp:114-122`, `tools/scan_server/main.cpp:394-400`]**,
  so freeing the listener together with the context is not safe; the earliest
  known-safe point to free it is process exit (the scan server keeps its
  listeners in static storage accordingly).
- Release with `obscan_device_management_remove_device_msg_listener` before
  destroying the listener. Registration is single-slot (§2.3), so an add/add
  pair fails rather than replacing.
- Messages emitted before registration are **dropped**; there is no queue.
- Delivery is on the emitting thread [hw-observed: the registration-driven
  DEVICE_CONNECT ran on the registering worker thread — §4].
- Device-object churn: remove→add re-enters device creation while the SDK may
  still hold the previous device object (§2). Every step returns 0, so the
  cycle is ABI-legal, yet the observed bookkeeping state ended in the §9
  NetDeviceWatcher fault. Until #11 clears the mechanism, a successful
  remove→add is **not** proof of safety.

## 9. Symbols without static reference — corrected by the crash report

`OBDeviceManagement::bind_device_connect_callback()` is **live**, despite
having no static caller: the faulting thread of the
2026-10-17-plan8-device-message-listener run carries the
`std::__1::__function::__func<… bind_device_connect_callback …>` frame
invoking `update_camera_status → update_device_info → get_active_device()`
while the vendor's NetDeviceWatcher (GVCP) thread re-delivers
DEVICE_CONNECT (crash.ips frame #12:
`libobsensor::pal::NetDeviceWatcher::start(...)::$_1`). The earlier
"remnant of an older wiring" reading is **retracted**: the function binds
the connect callback that runs both immediately at registration (§2) and
again from the NetDeviceWatcher thread.

**Reproduction count** [hw-confirmed: the five 2026-10-17 runs] — the
connect-callback SIGSEGV is not a one-off; the precondition is the OQ-3
remove/re-add sequence. Five crash reports on this binary and worktree carry
the same signature (`EXC_BAD_ACCESS`/SIGSEGV at `0x0`, faulting thread 5 = the
NetDeviceWatcher/GVCP thread), and every raw log shows the same sequence: a
`[worker][cb] DEVICE_CONNECT` delivery → `add(handle, listener_a) after
remove -> OB_SCAN_SUCCESS` (the re-add) → `ObException.cpp:5 "already
created"` → SIGSEGV. Named as the archive names them:

| report | run | the record's own words |
| --- | --- | --- |
| `2026-10-17-plan8-device-message-listener.crash.ips` | 16:54 | first crash; the analysis file reads its stack |
| `2026-10-17-device-message-listener-test.crash.ips` | 19:21 | same signature; that report's own destroy/libomp attribution was corrected by the main-thread re-read |
| `2026-10-17-device-message-listener-test-run2.crash.ips` | 20:01 | "reproduced for the second time today on this exact binary and worktree" |
| `2026-10-17-device-message-listener-test-run3.crash.ips` | 22:41 | "reproduces, for a third time (plan8 run, run2, run3)" |
| `2026-10-17-device-message-listener-test-run4.crash.ips` | 22:45 | "Controlled re-run (fourth run of this binary today)" |

So treat it as deterministic-ish under the OQ-3 remove/re-add sequence
(run3's wording). The single-registration flow (`f4c5f86`, the 2026-10-16
plan1 run) neither supports nor contradicts this: its record says it "only
covers what that tool happens to exercise: a single registration, never a
remove or a second add" — it never exercised the remove/re-add path and
predates all five observations.

**Main-thread state at the fault** [hw-confirmed:
`2026-10-17-crash-reports-main-thread-teardown-correction.md` — identical in all
three archived reports]: the main thread was inside `obscan_context_destroy()` →
`OBDeviceManagement::release()` → `~OrbbecDevice` → `~OrbbecInternalDevice` →
`OrbbecInternalDevice::stop()`, **blocked on a recursive_mutex** — teardown was
racing the fault, not absent (row #5's "never reached destroy" premise is
dropped). No report **in this series** shows `obscan_context_destroy()` itself crashing; the
f4c5f86 plan1 run's destroy returned SUCCESS in 807 ms with worker exit 0.

Still without any static or dynamic reference:
`OBDeviceManagement::send_device_msg(int)` and
`OBDeviceManagement::init_device_msg(int)` have no direct `BL`/`B` caller and
no `ADRP`+`ADD` address reference. Detection covered only those two forms, so
"unused" is **not proven** for those two.

---

## OPEN QUESTIONS

- **OQ-1** — `data` payload semantics. `send_device_msg(int)` supplies
  `data = this+0x167` (non-NULL, an interior pointer to the manager) for
  `msg_id == 0` and NULL for every other id, but `update_camera_status` emits
  through the struct overload with its own `data` values. Per-call-site payloads
  are not yet mapped. Nothing in the header should claim `data` is a device list
  until this is settled.
- **OQ-2** — *Resolved* (2026-10-17-plan9) → §6.1. The listener is a single delegate
  pointer at `0x00`, consumed by one load through the `__func` vtable chain at
  `__DATA_CONST 0x454d3b0`; the delegate ABI is the by-value message in
  `x0`/`x1` `[hw-confirmed: 2026-10-17-plan8-device-message-listener]`. The
  data/process families are byte-identical statically, and tips/control are
  ranged too — all five families share one invoke body (§6.1).
- **OQ-3** — Does `remove_device_msg_listener` fully clear the `std::function`
  (making a subsequent add succeed)? **Answered** — the native (`0x5c23f4`)
  loads `__f_` from `this+0xb8`, clears it (`str xzr, [x8,#0xb8]`), then calls
  the stored function's destroy slot (`+0x20` when `__f_` points at the inline
  buffer, else `+0x28`) and returns `0`; an empty slot logs and returns
  `845283593` (`OB_SCAN_ERROR_LISTENER_NOT_FOUND`). The `const` listener
  argument is **never read**. Raw:
  `.recon-cache/listings/OBDeviceManagement_remove_device_msg_listener.asm`
  (re-read this pass). `[hw-confirmed: 2026-10-17-plan8-device-message-listener]`
  — `[worker][PASS] add after remove == SUCCESS (OQ-3 cleared slot)`.
- **OQ-4** — Exact thread that invokes the listener (vendor device thread?), and
  whether any re-entrancy is possible from inside the callback. Needs dynamic
  tracing. **Partially resolved**
  (`2026-10-17-plan8-device-message-listener`): the registration-driven
  delivery ran on the registering worker thread (`14660842`) — raw.log:26
  prints callback-thread hash == registration-thread hash, and check 7's
  CONNECT (:75) carries the same hash — so every invocation recorded ran on
  the worker thread and none on a watcher thread. The vendor's
  NetDeviceWatcher (GVCP) thread (`14660873`) executes the same emitter but
  never invoked the listener in the recorded run — it faulted upstream of
  `send_device_msg` (§9). The re-entrancy half stays open → **#11**.
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

## 10. The NULL-state cause of the connect-callback SIGSEGV (row #11)

*Static half, plan11 pass, against the four-run crash signature (plan8 + run2/run3/run4,
identical stacks). The `.ips` `symbolLocation` fields pin the faulting instruction;
frame data from `2026-10-17-device-message-listener-test-run4.crash.ips`.*

**Which pointer is NULL: `OBDeviceManagement+0x158` — the manager's device pointer.**
The faulting frame is `device::OrbbecDevice::get_active_device()` at
**`symbolLocation: 0`** — its *first* instruction (`ldr x9,[x0]` @ `0x1b565c`), i.e. it
was called with `this == NULL`. Its caller is `OBDeviceManagement::update_device_info`
at `symbolLocation: 624` — the return site of `bl get_active_device` (`0x5c3580`),
whose argument was loaded one instruction earlier:

```
5c3574: ldr x0, [x27, #0x158]     ; x27 = the OBDeviceManagement instance
5c3580: bl  device::OrbbecDevice::get_active_device
```

So the NULL is the manager's device pointer at `+0x158` — the same field
`OBDeviceManagement::release()` clears (`str xzr,[x8,#0x158]`, `0x5c14ec`) before it
destroys the device (the row #4 teardown map). By the time the watcher's device-info
update ran, the pointer had been cleared.

**The destroy path takes no lock on that field; the watcher path takes a different
one.** `update_device_info` locks the manager's `std::mutex` at `+0x100` for its whole
body (`0x5c333c`), so its read of `+0x158` happens under that mutex. But:

- `release()` takes **no lock at all** — load, clear `+0x158`, call `~OrbbecDevice`
  (`0x5c14dc`–`0x5c14f0`);
- the block the main thread hits (`.ips`, all four reports) is inside
  `~OrbbecInternalDevice` → `stop()` (`0x1da7d8`), which locks the **internal
  device's `recursive_mutex` at `+0xf8`** (`0x1da80c`, stub `0x2ad7af0`) — a different
  object and a different lock class.

So the two paths share **no lock**: the destroy path can clear `+0x158` while the
watcher's `update_device_info` holds `+0x100` and is about to read it. That is the
race, mechanically — and it answers row #20's open question: `stop()`'s mutex is
**not** the one `update_device_info` takes. [static lock map; the fault's `this == NULL`
is `[hw-confirmed: the four .ips reports]`]

**Why a trailing device-changed event is in flight at teardown.** The watcher is a
periodic GVCP discovery poll: each poll that sees the enumerated set change drives
`onDeviceChanged → … → the bound callback`. The remove→re-add churn (#15's
remove-side race) leaves that set flapping (create → destroy → create, with the SDK's
own `Trying to create a device that you've already created!`), so further polls fire
device-changed events after the test's sequence has completed — run4 logs exactly such
an event (`22:45:47.318622`) *after* check 5c returned SUCCESS, racing the main
thread's entry into `obscan_context_destroy()`. **[inferred from the run log + the
frame chain; the SDK-side change detection lives in libOrbbecSDK's `DeviceManager`]**

**Can the caller avoid it?** The caller controls the *trigger*, not the race — the
unsynchronized clear is vendor-side. The churn that manufactures the trailing events
comes from the remove/re-add dance while the watcher is live, so: **do not remove and
re-add the listener around live watcher activity** (register once and keep it; this
project's tools now hold the listeners as process-lifetime statics and never call
`remove_*`), and if a remove is ever needed, let the device churn settle (well over a
poll interval) before `obscan_context_destroy()`. Teardown in a disposable worker
process remains the backstop.

## Evidence

Raw listings for this pass (disassembly, symbol tables, decoded tables) are in
`.recon-cache/listings/` — `OBDeviceManagement.region.asm`,
`obscan_dm_add_device_msg_listener.asm`,
`OBDeviceManagement_add_device_msg_listener.asm`,
`OBDeviceManagement_send_device_msg_msg.asm`,
`OrbbecInternalDevice_set_call_back.asm`, `classes.symbols.txt` — and
`.recon-cache/listings/liborbbec_scan_arm64.symbols.txt`. The OQ-2 evidence is
`.recon-cache/listings/device-msg-listener.monostub-vtable.plan9.asm`
(registration capture, vtable slots, send_device_msg vcall, and the
data/process byte-identical invokes). Per the wall in `docs/recon/README.md`
these are read-only for `builder` and are not reproduced here.

Hardware results carried in this file (kept in-repo under `tests/results/`):
`2026-10-17-plan8-device-message-listener.raw.log` (the Pioneer
created/destroyed/created timeline and both thread ids: registerer/deliverer
`14660842`, NetDeviceWatcher/faulting thread `14660873`),
`2026-10-17-plan8-device-message-listener.crash.ips` (the faulting stack), and
the reconcile analysis
`2026-10-17-plan8-device-message-listener-crash-analysis.md`; and
`2026-10-17-device-message-listener-test-run4.crash.ips` (run4's `.ips` — identical
stack; its `symbolLocation` fields pin the faulting instruction — §10). The five
crash reports of the series (§9): `2026-10-17-plan8-device-message-listener.crash.ips`,
`2026-10-17-device-message-listener-test.crash.ips`,
`2026-10-17-device-message-listener-test-run2.crash.ips`,
`2026-10-17-device-message-listener-test-run3.crash.ips`,
`2026-10-17-device-message-listener-test-run4.crash.ips`. Plus the
main-thread re-read `2026-10-17-crash-reports-main-thread-teardown-correction.md`
(the three archived `.ips` reports' main-thread stacks) and the single-registration
run `2026-10-16-plan1-device-message-listener.md` (destroy SUCCESS, 807 ms, exit 0 —
it never exercised remove/re-add, so it neither supports nor contradicts the
crash claims; §9).
