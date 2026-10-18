# Data- and process-message listeners before `obscan_scan_preview` — PLAN #2

**Target:** `liborbbec_scan.3.5.3.dylib`, arm64 slice
(sha256 `184d92a87bdebba1cb5207ee1a111d49aa497f46088cc9f12d9abd3b59c017ec`),
same proof slice as PLAN #1.

**Status:** `SPEC-READY`

**Method:** static. Ghidra (REA 12.1.4) over the persisted snapshot
`.recon-cache/liborbbec_scan_arm64.analysis.json`; complete external symbol
table cross-check (`nm -gU`, file
`.recon-cache/listings/liborbbec_scan_arm64.symbols.txt`); decode of the
vendor's own `*_name` lookup tables. No dynamic tracing in this pass — all
runtime-semantics statements that are readings of intent rather than code carry
a tag, as in `device-message-listener.md`.

**Provenance note:** an early draft of this specification's header section was
swept into a `build:`-labelled commit (`5835ba6`) and reverted (`39e9156`); the
PLAN #2 material now lives here and in this pass's `recon:`-labelled header
change via the branch `recon/plan7-detection-path`. This file supersedes it.

---

## 1. The entry points

All four C entry points are exported and verified in the real binary's dynamic
symbol table (`T`, unmangled):

| C symbol | wrapper addr | native member | shape |
| --- | --- | --- | --- |
| `obscan_context_add_data_msg_listener` | `0xbb04` | `OrbbecScanContext::add_data_msg_listener(OBScanDataMessageListener_*)` @ `0x1b690` | `(context, listener)` → status |
| `obscan_context_remove_data_msg_listener` | `0xbb7c` | `OrbbecScanContext::remove_data_msg_listener(OBScanDataMessageListener_ const*)` @ `0x1b9dc` | `(context, listener)` → status |
| `obscan_context_add_process_msg_listener` | `0xbbf4` | `OrbbecScanContext::add_process_msg_listener(OBScanProcessMessageListener_*)` @ `0x1ba68` | same |
| `obscan_context_remove_process_msg_listener` | `0xbc6c` | `OrbbecScanContext::remove_process_msg_listener(OBScanProcessMessageListener_ const*)` @ `0x1bdb4` | same |

Mangled types read from the exports:

```
_ZN17OrbbecScanContext21add_data_msg_listenerEP26OBScanDataMessageListener_
_ZN17OrbbecScanContext24remove_data_msg_listenerEPK26OBScanDataMessageListener_
_ZN17OrbbecScanContext24add_process_msg_listenerEP29OBScanProcessMessageListener_
_ZN17OrbbecScanContext27remove_process_msg_listenerEPK29OBScanProcessMessageListener_
```

**The two `remove_*` signatures are therefore read, not inferred** — including
the `const` on the listener pointer (the mangled `K` qualifier). The wrappers
`tail-b` straight into the members after an entry log (`bl 0x164dd0` shared
logger; context label printed after the call), with one shared precondition:
`cbz` on the context handle aborts with `0x32620107` (`845283591`,
`OB_SCAN_ERROR_DEADLINE_EXCEEDED`), the same generic null-handle code as every
`obscan_device_management_*` wrapper (see `device-message-listener.md` §5 —
not a timeout).

There are **no** context-level `tips` / `control` / `calib` listener pairs in
the export table; those families are device-management-side only (PLAN #1 §1).

## 2. Registration semantics (native, both families mirror-image)

All state lives on `obscan_common::OBScanParasContext`, reached as
`OrbbecScanContext` `this+0x8`:

| family | `std::function` home | empty-slot probe | capture stub |
| --- | --- | --- | --- |
| data | `paras+0x50` | `paras+0x68` | `0x4539308` |
| process | `paras+0x70` | `paras+0x88` | `0x4539398` |

The homes are 32-byte libc++ `std::function` objects; the probe word is
`home+0x18`, the function's callable pointer, and the add path validates it
against `0x0`. Guards, in order:

1. **NULL listener** and **already installed** funnel to the same failure:
   log `<FAMILY> is already set !` plus return `0x32620100`
   (`845283584`, `OB_SCAN_ERROR_INVALID_ARGUMENT`). **Registration is
   single-slot.** (Log literal addresses: data `0x42df091`, process
   `0x42df148`.)
2. **Install**: the target is constructed as a stack pair
   `{stub, caller's listener pointer}` (data stub `0x4539308`, process stub
   `0x4539398`) and assigned into the home (`bl 0x1b870`). The listener is
   **referenced, not copied**.
3. **Success** logs `add <FAMILY> success !` and returns `0`.
4. **Remove on an empty slot** logs `<FAMILY> is not set !` and returns
   `0x32620109` (`845283593`, `OB_SCAN_ERROR_LISTENER_NOT_FOUND`).
5. **Remove success** stores `xzr` over the probe word and destroys the
   `std::function` through its slot vtable (offsets `0x20/0x28`), so a later
   add succeeds. This is the same clear-and-destroy pattern PLAN #1 could not
   confirm for the device family (**OQ-3** there): for the data and process
   families it is **recovered, not assumed**.
6. Any concurrent add/remove/deliver race is uncovered — see **OQ-P2**.

## 3. The preview gate — BOTH listeners required

`obscan_scan_preview` (wrapper @ `0xf608`) transitions:

```
obscan_scan_preview                       0xf608   NULL scan → 0x32620107
  OBScanBase::begin_async_task            0x6fc468 task kind 0x14
  OBScanBase::preview                     0x6fa850
  OBScanBase::abandon_async_task          0x6fc670 (status==0 path)
```

The join point is `OBScanBase::judge_can_preview` @ `0x6fa674`:

- gate A: `[this+0xe1]` bit0 must be set, else
  `0x32620110` (`845283600`, `OB_SCAN_ERROR_MULTI_CALLED`);
- gate B: state word at `[this+0x4c8]` read with **LDAR** must be in
  `{1, 3, 4, 5}`, else `OB_SCAN_ERROR_MULTI_CALLED`;
- device/cfg checks (helpers `0x702ee4 / 0x70288 / 0x5c1010 / 0x5c49cc`) whose
  log labels sit at `0x42fa0dec / 0x42fa0e5a` — **not recovered** (the
  `0x42fa0xxx` string page is unmapped in both the Ghidra view and the file:
  see **OQ-P4**);
- finally it loads the paras object at `[this+0x448]` and calls
  `obscan_common::OBScanParasContext::judge_is_exist_listener` @ `0x6aa6b8`,
  which requires **both** slots — data probe `+0x68` and process probe
  `+0x88`, logging the deficient family ("data listener" / "process
  listener") — and on any miss the preview path computes
  `0x32620110 − 7 = 0x32620109` (`845283593`,
  `OB_SCAN_ERROR_LISTENER_NOT_FOUND`).

So the caller contract is: **register both `add_data_msg_listener` and
`add_process_msg_listener` before `obscan_scan_preview`**; a missing either is
rejected with `OB_SCAN_ERROR_LISTENER_NOT_FOUND`, not with an argument error.
The earlier header wording ("data mandatory, per the app's log") is superseded:
both are mandatory by construction.

## 4. Delivery paths (they differ!)

`obscan_common::OBScanParasContext` emits both families:

- **`send_data_msg(OBScanDataMessage_)` @ `0x6a9fcc`** — 13 instructions,
  complete: load probe `+0x68`, `cbz` → **return silently, message dropped,
  no log**; otherwise copy the 16-byte message to the stack with one
  `stp x1,x2` and invoke the `std::function` via slot vtable `+0x30`.
  **Synchronous, direct dispatch on the emitting thread; drop-at-emit.**
- **`send_process_msg(OBScanProcessMessage_)` @ `0x6aa1b8`** — 215-
  instruction body: resolves `OBScanProcessMessageID_name(msg_id)` and logs
  `send process msg id : %s` (label `0x42f7362`), then builds the
  the 40-byte closure `{vptr 0x454dc60, this, message}` and submits it with
  `rs_common::thread_pool_enqueue_task(function&, future&)` @ `0x16b2f4`.
  Submit failure (`w0` bit0 clear) logs `cannot enqueue task process msg
  process, perhaps thread pool is down` (`0x42f7379`); the returned
  `future` is pushed into the vector at `this+0x90/0x98/0xa0` — a
  **futures registry**, not a task queue (nothing drains it; it is destroyed
  with the paras). **Asynchronous dispatch via the shared thread pool**; the
  drain thread and the emptied-slot behavior are answered in §10 (OQ-P1).
- **The four stream wrappers** — `send_rgb_msg` / `send_depth_msg` / `send_ir_msg` /
  `send_frame_msg` (@ `0x6a9f3c` / `0x6aa000` / `0x6aa090` / `0x6aa120`) — are the observed
  per-frame delivery paths (§12). Each checks the data probe, pushes the payload's
  `shared_ptr` into a per-stream mutex-guarded ring (capacity 10), then builds
  `{id, pad, data = shared_ptr.get()}` and calls the listener **inline** (the same
  probe+call shape as `send_data_msg`; the generic `send_data_msg` has no direct callers).

`OBScanProcessMessageID_name` is *always* evaluated inside the emitter — so at
emit time the id passes through the same jump table decoded in §5 below.

## 5. Message-ID vocabulary — complete

### Data family — `_OBScanDataMessageID_name` @ `0x1714a4` (compare chain)

| value | hex | name string |
| --- | --- | --- |
| `0` | — | `OB_SCAN_MESSAGE_ID_NEW_RGB_IMAGE` (`0x42ebc2c`) |
| `325189888` | `0x13620100` | `OB_SCAN_MESSAGE_ID_NEW_DEPTH_IMAGE` (`0x42ebc4d`) |
| `325189889` | `0x13620101` | `OB_SCAN_MESSAGE_ID_NEW_IR_IMAGE` (`0x42ebc70`) |
| `325189890` | `0x13620102` | `OB_SCAN_MESSAGE_ID_FRAME_READY` (`0x42ebc90`) |
| default | — | `UNKNOWN` (`0x42dfae9`) |

The header's data values (`325189888–890`) match the decode digit-for-digit.

### Process family — `_OBScanProcessMessageID_name` @ `0x171594` (jump table)

Index arithmetic is `msg_id − 0x14620100`, legal range 0..5; pointer table at
`0x45458a0`, strings `0x42ebcd2…0x42ebd9e`:

| value | hex | name string |
| --- | --- | --- |
| `341967104` | `0x14620100` | `OB_SCAN_MESSAGE_ID_FRAME_BUFFER_FULL` |
| `341967105` | `0x14620101` | `OB_SCAN_MESSAGE_ID_MAX_FRAMES_REACHED` |
| `341967106` | `0x14620102` | `OB_SCAN_MESSAGE_ID_DETECT_FACE` |
| `341967107` | `0x14620103` | `OB_SCAN_MESSAGE_ID_SCANNING_FAILED` |
| `341967108` | `0x14620104` | `OB_SCAN_MESSAGE_ID_OPTIMIZATION_TO_MESH_NEED_CHANGE_VOXEL_SIZE` |
| `341967109` | `0x14620105` | `OB_SCAN_MESSAGE_ID_PASTED_MARKERS_TOO_REGULAR` |
| default | — | `UNKNOWN` |

All six name literals verified present in the shipped dylib by
`strings` scan **in this pass** (alongside earlier physical-run evidence for
`341967109`). **None of the seven items the header now carries is inferred**:
the six IDs come from the vendor's own table decode, and both `remove_*`
signatures come from the exported mangled types (§1).

Family encoding continues the PLAN #1 pattern: device `0x1262`, data `0x1362`,
process `0x1462`, with the low byte as the member index
(`device-message-listener.md` §7 — same tag applies: **[pattern inferred from
three families]**).

## 6. Struct layout

`OBScanDataMessage_` — **16 bytes, verified at the send site** (`send_data_msg`
materialises the message with a single 16-byte store before invoking the
function):

| offset | type | field |
| --- | --- | --- |
| `0x00` | `int32` | `msg_id` |
| `0x04` | `int32` | padding |
| `0x08` | `void*` | `data` — per-id payload pointer; for the four stream ids it is the raw `OBScanImage_*` / `OBScanFrame_*` (§12) |

`OBScanProcessMessage_` is declared with the same three-field shape; the send
path passes it by registers through the queued closure. The 16-byte
materialization is confirmed for the data family specifically; the process
struct's runtime size is **[inferred — same construction family and C#
declaration, not yet natively sized]**.

The stored listener remains the C#-shaped single-delegate struct. The "capture
stubs" are the `__func` vtables of the registered `std::function`s (not stub
objects); their `operator()` slots were disassembled on 2026-10-17-plan9 and
are **byte-identical to the device family's** (`0x1b7ec` for data, `0x1bbc4`
for process, both ending `ldr x2,[x9]; mov x0,x8; br x2` with
`x9 = capture0 = listener`) — one load through `[listener+0x00]`, so the
per-family shape question is settled for these two families — see §6.1 of
[`device-message-listener.md`](device-message-listener.md). Tips and control
were ranged in the follow-up pass and are byte-identical too — see §6.1 there.

## 7. Lifetime rules

- The listener object is referenced inside the stored `std::function` and must
  stay alive until `remove_*` succeeds — and past `obscan_context_destroy()`:
  the SDK's background threads can outlive destroy while still holding the
  captured pointer **[inferred: project threading model —
  `engine/src/obscan_loader.cpp:114-122`]**, so freeing it together with the
  context is not safe; the earliest known-safe point to free it is process
  exit. Same rule as the device family —
  [`device-message-listener.md`](device-message-listener.md) §8.
- `remove_*` clears the probe word and destroys the function (§2.5) — after
  which a fresh add succeeds. There is no queue, so nothing booked-but-undelivered
  survives a remove (data path); the process path's queued closures that drain
  after a remove are dropped by the closure's own probe (§10).
- **No synchronization on the slots** (§11): the probe check, the install swap, the
  clear and the destroy are all plain accesses — a `remove_*` must not race live
  delivery.
- Messages emitted with no listener installed are dropped by the emitter
  itself (data: at emit; process: at drain — §10).
- Same single-slot, single-registered-listener discipline as the device family.

## 8. Error codes (these families)

| condition | code | symbol |
| --- | --- | --- |
| NULL context (all four wrappers) | `845283591` | `OB_SCAN_ERROR_DEADLINE_EXCEEDED` — generic null-handle precondition |
| NULL listener / already installed | `845283584` | `OB_SCAN_ERROR_INVALID_ARGUMENT` |
| remove on empty slot | `845283593` | `OB_SCAN_ERROR_LISTENER_NOT_FOUND` |
| listener missing at preview | `845283593` | `OB_SCAN_ERROR_LISTENER_NOT_FOUND` |
| success | `0` | — |

—and both `judge_can_preview` upstream gates fail with `845283600`
(`OB_SCAN_ERROR_MULTI_CALLED`), a distinct signal most notable from the
`[+0x4c8]` lifecycle state (`OQ-P4`).

## 9. Symbols observed without any reference

As in PLAN #1 §9, detection covered `BL`/`B` and `ADRP`+`ADD` forms; nothing
in this pass relied on an unreferenced symbol.

## 10. Process-family drain: the pool worker, and drop on an emptied slot (OQ-P1, answered)

*Static half, plan26 pass; raw excerpts appended to the plan2 dossier. Addresses from
the pinned slice.*

**Which thread drains the closure.** `send_process_msg` submits the 40-byte
`__func<__bind<send_process_msg::$_0>>` closure (`{vptr 0x454dc60, paras, message}`)
via `rs_common::thread_pool_enqueue_task` @ `0x16b2f4`, which enqueues into the
**global `rs_common` thread pool** — a `BS::thread_pool<tp::0>` singleton at
`0x492ac28` (initialized flag `0x492ac30`). The singleton is created by
`rs_common::thread_pool_create_instance` @ `0x16a5f8` from `ObscanContextBase::C2`
(`0x6f3f48`) — i.e. **at `obscan_context_create`**, with a thread count computed there
(16-bit value; derivation not traced) — and destroyed by `OrbbecScanContext::~`
(`thread_pool_destroy_instance` @ `0x16dfa0`). `create_instance` also makes four
*named* pools — "default" ×2, "laser" ×1, "blae" ×1, "db_write" ×1 — **not** used by
this path. The closure's `operator()` therefore runs on **a worker thread of the
global pool**, asynchronously to the emitter. If the pool is down (before context
create / after destroy), the submit returns 0 and the message is dropped with the
warning log. The workers are BS::thread_pool's internal threads; their concrete OS
identity is a runtime property (below).

**A drained task whose slot was emptied: DROP.** The closure body
(`__func<…send_process_msg…>::operator()` @ `0x6aa608`, complete):

```
mov x8, x0                 ; the closure
ldr x9, [x0, #0x8]         ; capture0 = the paras context
ldr x0, [x9, #0x88]        ; the process slot's __f_ probe word
cbz x0, 0x6aa644           ; EMPTY -> skip: silent drop, no call, no throw
ldr q0, [x8, #0x10] / str q0, [sp]      ; the captured 16-byte message
ldr x8, [x0] / ldr x8, [x8, #0x30] / blr x8   ; listener(message)
```

At drain time the closure re-checks the **same probe word
`remove_process_msg_listener` clears** (`paras+0x88`, `STR XZR`): a task that drains
after a remove is **silently dropped** — it never reaches
`std::__1::__throw_bad_function_call`. (Data family: drop-at-emit. Process family:
drop-at-drain.) The racy window — probe read non-null, then the function destroyed by
a concurrent remove before the call — is **OQ-P2**, not this question.

**Dynamic trace: not needed to close OQ-P1.** Both halves are settled statically
(which thread *class* runs the closure; the drop). What static analysis cannot give is
the concrete OS thread identity of the pool workers — an observation, not a gate; it
can ride along with row **#31**'s listener instrumentation (log the thread id per
delivery there). If a dedicated run is still wanted, the precise spec: (a) instrument
the process-message listener callback to log `pthread_threadid_np` (or the tool's
thread-hash) per delivery; (b) trigger deliveries — the queue-full condition emits
`FRAME_BUFFER_FULL` process messages during scanning; (c) observe that the callback
thread differs from the tool's main thread and from the vendor watcher threads
(`14660842` / `14660873` in the plan8 run) — i.e., a pool worker. The drop path is not
externally triggerable in a controlled way (it is an enqueue→remove→drain race); the
static evidence suffices — do not spend the serialized hardware room on it.

## 11. Slot-home and queue synchronization: no lock on the slots (OQ-P2)

*Static half, plan27 pass; raw excerpts appended to the plan2 dossier. Instruction-level read
of the four registration entry points, both emitters, and the pool's internals.*

**What is guarded.** The **pool's task queue** is properly locked:
`BS::thread_pool<tp::0>::detach_task` (@ `0x16b3d4`) takes `std::mutex::lock`/`unlock`
(stubs `0x2ad7d48`/`0x2ad7d54`) around the deque push, and `…::worker` (@ `0x16b0xx`) takes
the same mutex plus `condition_variable::wait`/`notify_all`. The task hand-off itself is
thread-safe.

**What is not guarded: everything else.** The slot homes and the paras futures vector use
plain loads/stores — no mutex, no atomics, no acquire/release, in any of the six operations:

| op | address | probe access | install/clear | lock |
| --- | --- | --- | --- | --- |
| `add_data_msg_listener` | `0x1b690` | `ldr x9,[paras+0x68]` (plain) | `__value_func::swap` into `paras+0x50` | none |
| `remove_data_msg_listener` | `0x1b9dc` | `ldr x0,[paras+0x68]` (plain) | `str xzr,[paras+0x68]` **then** destroy (`+0x20` inline / `+0x28` heap) | none |
| `send_data_msg` | `0x6a9fcc` | `ldr x0,[paras+0x68]` (plain) | — (calls via `[x0]`+`0x30`) | none |
| `add_process_msg_listener` | `0x1ba68` | `ldr x9,[paras+0x88]` (plain) | swap into `paras+0x70` | none |
| `remove_process_msg_listener` | `0x1bdb4` | `ldr x0,[paras+0x88]` (plain) | `str xzr,[paras+0x88]` **then** destroy | none |
| process closure (drain, §10) | `0x6aa608` | `ldr x0,[paras+0x88]` (plain) | — | none |

A region-wide scan for `mutex`/atomics across the paras class (`0x6a9000–0x6ab000`) and the
registration methods (`0x1b600–0x1bf00`) finds **no** synchronization at all (the only hit is
an unrelated `mutex::try_lock` in `read_specify_format_data`). The C wrappers
(`0xbb04`/`0xbb7c`/`0xbbf4`/`0xbc6c`) only log and tail-branch. The paras futures vector
(`+0x90/0x98/0xa0`) push in `send_process_msg` is likewise plain.

**The racy windows (by construction).**

1. **deliver vs remove** — the window deferred from §10: deliver loads the probe (non-null),
   remove clears it and destroys the function, deliver calls through the stale `__f_`. Heap
   case: the `__func` is `operator delete`d → use-after-free. Inline case: `destroy()` is a
   no-op but the home buffer is reused by a later `add`'s swap → call through a half-swapped
   object. Both families share the shape (data: at emit; process: at drain).
2. **add vs deliver** — the install is a swap of the home; a deliver holding the old `__f_`
   can execute against the object as it is swapped out.
3. **add vs add / add vs remove** — the probe check is check-then-act: two concurrent adds
   both pass "not set"; an add racing a remove can install into a slot being cleared.
4. **emitter vs emitter on the futures vector** — two concurrent `send_process_msg` calls race
   the vector push (and its capacity growth) with no lock. Reachability depends on the vendor
   emitting process messages from more than one thread — unobserved. **[inferred]**

**Conclusion for callers: there is no lock to rely on.** A `remove_*` must not race live
delivery; this project's own usage (listeners as process-lifetime statics, no `remove_*`
call) never enters these windows. The spec must not promise thread-safety for add/remove
against delivery.

**Dynamic trace: not needed to close OQ-P2** — the static answer is complete (no locks; the
windows exist by construction) and the actionable guidance does not depend on observing a
crash. If a stress confirmation is ever wanted, the precise spec: (a) in a hardware run,
spawn a second thread hammering `add → remove → re-add` on both families while a scan is
active and frames flow; (b) callbacks do a short busy-loop to widen the deliver window;
(c) watch for a crash (`.ips`) or a corrupted delivery; (d) additionally log the emitter
thread id in the data/process callbacks to settle window 4's reachability. A negative result
proves little (the window is a handful of instructions); do not spend the serialized room on
it without a specific suspicion.

## 12. Data-message payload semantics per emitter (OQ-P3, answered)

*Static half, plan28 pass; raw excerpts appended to the plan2 dossier. The dynamic record
`tests/results/2026-10-18-scan-server-data-process-instrumentation.md` observed the four ids
below as per-frame triples on one vendor thread, payload pointers striding 0x60.*

**Per-id payload map.**

| id | emitter | `data` points at |
| --- | --- | --- |
| `0` `NEW_RGB_IMAGE` | `send_rgb_msg(shared_ptr<OBScanImage_>)` @ `0x6a9f3c` | `OBScanImage_*` |
| `325189888` `NEW_DEPTH_IMAGE` | `send_depth_msg` @ `0x6aa000` | `OBScanImage_*` |
| `325189889` `NEW_IR_IMAGE` | `send_ir_msg` @ `0x6aa090` | `OBScanImage_*` |
| `325189890` `FRAME_READY` | `send_frame_msg(shared_ptr<OBScanFrame_>)` @ `0x6aa120` | `OBScanFrame_*` |

All four inline the same dispatch as `send_data_msg` (probe `paras+0x68` → drop if clear;
build `{msg_id, pad, data}` on the stack; call the listener on the emitting thread) and first
push the payload's `shared_ptr` into a **per-stream ring** (`msg_buffer_push_impl<T>` @
`0x6b0850`: a mutex-guarded `std::list<shared_ptr<T>>`, **capacity 10** — evict-oldest or
fail-and-warn). The generic `send_data_msg` @ `0x6a9fcc` has no direct callers in this binary;
the stream wrappers are the emitters for these ids.

**The payload objects — both 88 bytes (0x58; malloc rounds to the observed 0x60 stride).**

| offset | field |
| --- | --- |
| `+0x00`/`+0x08` | `shared_ptr<Impl>` — for the frame the impl is `OBScanFrameInterface` (`new(0x2e0)`); for the image, the image-data impl |
| `+0x10` | `uint16` state: bit0 = has data; bit1 = **consumed**; `0` = released |
| `+0x18` | embedded `pthread_mutex_t` (sig `0x32AAABA7`), 64 bytes to `+0x57` |

**Ownership/lifetime: borrowed, one-shot, ring-kept.** `data` is the raw pointer from the
shared_ptr — no refcount bump. The object stays alive while its stream ring holds it (10
deep); `OBScanFrame_::release_data` @ `0x6b2200` sets state 0 and drops the impl. Every
accessor is **one-shot**: `get_frame_cloud` and the image getters lock `+0x18`, check the
state, set bit1, then call the impl — a second accessor call on the same object fails with
`845283600` (`OB_SCAN_ERROR_MULTI_CALLED`, "…multi called !"), and after release with
`845283584` ("…released !"). Consume synchronously, one accessor call, do not retain.

**FRAME_READY carries a consumable cloud — the §6 revisit trigger has fired.**
`OBScanFrame_::get_frame_cloud` @ `0x6b2344` fills an `OBScanPointClouds_`-shaped struct
inside the impl and returns it: points vector `impl+0xa0` → `count@+0x100`, `begin@+0x108`;
normals `impl+0x150` → `count@+0x1b0`; colors `impl+0x198` → `count@+0x2b0`; reference rods
from `impl+0x1d0`. The C API exports the path: `obscan_frame_get_frame_cloud(frame,
OBScanPointClouds**)` @ `0x107fc` (validates both pointers; tail-branches to the native). So
the listener channel **can** deliver a **per-frame** point cloud. Per `scan-live-data.md` §6
the live-path decision should be re-taken: this path is per-frame (lower latency,
vendor-faithful) but is not the accumulated model cloud the current UI renders — an
accumulated view would need our own accumulation. The `obscan_frame_*` surface is **not in
our header subset**; building on this path needs a contract row first (the
`obscan_frame_get_frame_cloud` + `obscan_frame_release` pair, mirroring #24).

**Dynamic trace.** The layouts are static; what a run would add is the *value* per frame —
whether the frame cloud is populated live and with how many points. Spec (builder + hw,
after the frame API lands in the header/loader): in the data listener, on `FRAME_READY` call
`obscan_frame_get_frame_cloud` once and log `{num_points, first point}`; on one image id,
call one image getter (e.g. `obscan_image_get_width`) and log; expect per-frame counts
`> 0` and the one-shot behaviour (`MULTI_CALLED` on a second call). This is optional
confirmation — the semantics above do not depend on it.

## Evidence

Raw evidence: `.recon-cache/listings/data-process-listeners.plan2.asm` (wrapper
bodies, native registration bodies, both name tables, crossing emitters, the
preview-guard chain, the address-map bias test, and — plan26 pass — the
process-family drain: closure body @ `0x6aa608`, `thread_pool_enqueue_task` @
`0x16b2f4`, `thread_pool_create_instance` @ `0x16a5f8` and its caller @ `0x6f3f48`;
plan27 pass — the slot-home synchronization read: the four registration bodies, both
emitters, and the pool's mutex/condvar stubs; plan28 pass — the payload semantics: the
four stream emitters, the ring push impl, both payload-object ctors, the one-shot
accessors, and the frame-cloud path),
plus
`.recon-cache/listings/device-msg-listener.monostub-vtable.plan9.asm` for the
plan9 shard covering this file's data/process `operator()` windows. Export
cross-checks:
`.recon-cache/listings/liborbbec_scan_arm64.symbols.txt`. Analysis snapshot:
`.recon-cache/liborbbec_scan_arm64.analysis.json` (read-only; always `open`
with this snapshot, never re-analyze). Per `docs/recon/README.md` none of the
raw listings are reproduced here.

## OPEN QUESTIONS

- **OQ-P1** — *Answered* (plan26 pass) → §10. The closures run on a worker of the
  global `rs_common` thread pool (created at `obscan_context_create`, destroyed with
  the context); a drained task whose slot was emptied **drops silently** — the
  closure probes `paras+0x88` itself before calling. No dynamic trace needed; the
  optional thread-id observation folds into row #31.
- **OQ-P2** — *Answered* (plan27 pass) → §11. No lock guards the slot homes or the
  paras futures vector — all six operations use plain `ldr`/`str` (the pool's own queue
  is `std::mutex`-guarded; the slots are not). The deliver-vs-remove window (probe read
  → clear+destroy → call through stale `__f_`) is real by construction; a `remove_*`
  must not race live delivery. No dynamic trace needed to close; a stress spec is
  recorded in §11 if ever wanted.
- **OQ-P3** — *Answered* (plan28 pass) → §12. `data` is the raw `OBScanImage_*`
  (rgb/depth/ir) or `OBScanFrame_*` (frame) pointer, borrowed, one-shot accessors,
  ring-kept; `FRAME_READY` yields a per-frame cloud via `obscan_frame_get_frame_cloud` —
  the `scan-live-data.md` §6 revisit trigger has fired (decision re-take recommended).
  The frame C API is not yet in our header subset (follow-up row).
- **OQ-P4** — the `0x42fa0xxx` log-label page is double-unmapped (absent from
  the Ghidra view and from the file at that bias, despite proving
  identity-mapping earlier on the `UNKNOWN` string @ `0x42dfae9`): recover
  those labels if possible; also follow the `[+0xe1]` enable flag and the
  `[+0x4c8]` state machine (`{1,3,4,5}` membership) and the `[+0xe1]` enable flag for exact preview
  lifecycle semantics.
- **inherited OQ-2** (PLAN row #9) — *Resolved* for data & process
  (2026-10-17-plan9): both `__func` vtables' `operator()` invokes are
  byte-identical to the device family's — one load through `[listener+0x00]`
  and nothing else. See §6 (above) and §6.1 of
  [`device-message-listener.md`](device-message-listener.md); tips/control
  stay `[inferred]`.
