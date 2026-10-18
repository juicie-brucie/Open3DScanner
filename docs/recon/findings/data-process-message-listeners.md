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
  `{0x454dc60, this}` closure and submits it with
  `rs_common::thread_pool_enqueue_task(function&, future&)` @ `0x16b2f4`,
  pushing the closure into the vector at `this+0x90/0x98/0xa0`. Submit
  failure logs `cannot enqueue task process msg process, perhaps thread pool
  is down` (`0x42f7379`). **Asynchronous dispatch via the shared thread
  pool.** Which thread drains it, and whether a drained task with an
  emptied slot drops or throws like the device family
  (`std::__1::__throw_bad_function_call`), is **not recovered** — the body
  contains no direct function probe (`OQ-P1`).

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
| `0x08` | `void*` | `data` — opaque, payload semantics unverified (`OQ-P3`) |

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
remain analogy-only (**[inferred]**; noted as a follow-up there).

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
  survives a remove (data path); the process path's queued closures may still
  be drained after a remove (**OQ-P1**).
- Messages emitted with no listener installed are dropped by the emitter
  itself (data: at emit; process: at drain — `OQ-P1`).
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

## Evidence

Raw evidence: `.recon-cache/listings/data-process-listeners.plan2.asm` (wrapper
bodies, native registration bodies, both name tables, crossing emitters, the
preview-guard chain, and the address-map bias test), plus
`.recon-cache/listings/device-msg-listener.monostub-vtable.plan9.asm` for the
plan9 shard covering this file's data/process `operator()` windows. Export
cross-checks:
`.recon-cache/listings/liborbbec_scan_arm64.symbols.txt`. Analysis snapshot:
`.recon-cache/liborbbec_scan_arm64.analysis.json` (read-only; always `open`
with this snapshot, never re-analyze). Per `docs/recon/README.md` none of the
raw listings are reproduced here.

## OPEN QUESTIONS

- **OQ-P1** — process-family delivery plumbing: which thread drains the
  `+0x90/0x98/0xa0` queue, and does a drained task with an empty slot drop the
  message or throw `bad_function_call` (the body contains no explicit
  function-validity probe)? Needs the `{0x454dc60, this}` closure decode and
  thread-pool drain tracing.
- **OQ-P2** — add/remove/deliver race: no lock evidence anywhere on the
  slot homes or the queue vector; is the pair of probe-clear + function
  destroy atomic as seen by the emitters? Needs a dynamic trace
  (add/remove/re-add from a second thread while frames flow).
- **OQ-P3** — `data` payload semantics per emitter (same shape as the device
  family's **OQ-1**): `send_data_msg` passes the message through with an
  opaque `data` pointer; who dereferences it is unknown; do not
  document `data` as anything concrete until settled.
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
