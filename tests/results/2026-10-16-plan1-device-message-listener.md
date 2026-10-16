# Hardware test — PLAN #1: device-message listener registration

**Date:** 2026-10-16
**Device:** Creality Raptor Pro (CR0886100SN), USB3.0
**Program used:** `tools/live_capture_test` (existing tool, pre-dates the recomp
workflow — no `tests/hardware/*` program exists yet for this plan item; no builder
session has run. This result was gathered by driving the existing tool and reading
its console output rather than from a purpose-built test, so it only covers what
that tool happens to exercise: a single registration, never a remove or a second
add. See "Not covered" below.)
**Spec under test:** `docs/recon/findings/device-message-listener.md` (PLAN #1,
status `SPEC-READY` as of this run)
**Result:** `OB_SCAN_SUCCESS` throughout; full run completed cleanly (worker exit
code 0, `obscan_context_destroy` returned in 807ms with no power cycle needed).

## What was run

One full `obscan_context_create` → `obscan_device_management_add_device_msg_listener`
→ (wait for device) → scan lifecycle → teardown pass, via the existing
`live_capture_test` tool, rebuilt against the current (PLAN #1-updated) header
before running. Full console output: `2026-10-16-plan1-device-message-listener.raw.log`
(same directory). Relevant excerpt:

```
[worker] obscan_context_create                         -> OB_SCAN_SUCCESS (0)
[worker] obscan_context_get_device_management          -> OB_SCAN_SUCCESS (0)
[worker] obscan_context_add_data_msg_listener          -> OB_SCAN_SUCCESS (0)
[worker] obscan_context_add_process_msg_listener       -> OB_SCAN_SUCCESS (0)
[worker] device message: DEVICE_DISCONNECT (msg_id=308412672) data=0x0
[worker] obscan_device_management_add_device_msg_listener -> OB_SCAN_SUCCESS (0)
...
[worker] device message: DEVICE_CONNECT (msg_id=0) data=0x102bc25d7
[worker] obscan_device_management_get_device_num -> OB_SCAN_SUCCESS, count=1
```

## Observed vs. expected

| Spec claim | Expected | Observed | Verdict |
| --- | --- | --- | --- |
| §2: registration delivers a synchronous message before the native call returns | A device message fires *inside* `add_device_msg_listener`, before the wrapper's own status is available to the caller | `DEVICE_DISCONNECT` message printed **before** the `obscan_device_management_add_device_msg_listener -> OB_SCAN_SUCCESS` line — the callback fired and flushed stdout before the C wrapper's return value was even logged | **Confirmed.** Delivery is synchronous relative to the registration call, on real hardware, not deferred to a later poll/pump. |
| §2 + OQ-1: the synchronous message on registration is a DISCONNECT (device list empty at that instant), separate from the real CONNECT once the device is actually found | `msg_id=308412672` (`DEVICE_DISCONNECT`) at registration time | Exact match: `msg_id=308412672` | **Confirmed.** This also corrects this project's own earlier (pre-recon) working assumption in `tools/live_capture_test/main.cpp`'s comments, which described the first callback as "a non-actionable registration ack" with msg_id=308412672 — it's a real, correctly-identified `DEVICE_DISCONNECT`, not an ack. Worth updating that comment (recon/header territory, not done here). |
| OQ-1: `data` is NULL for every message id except `msg_id==0` | `data == NULL` for the DISCONNECT | `data=0x0` | **Confirmed** for this one message id. |
| OQ-1: `data = this+0x167` (non-NULL, interior pointer) for `msg_id==0` (CONNECT) | Non-NULL pointer for the real CONNECT | `data=0x102bc25d7` (non-NULL) once the device was actually found | **Consistent** — non-NULL as predicted. Did not verify it's literally `this+0x167` (would need the live `this` pointer value, not printed by the existing tool) — partial confirmation only. |
| §5: listener-family error codes (`INVALID_ARGUMENT` for NULL-listener/already-registered, `DEADLINE_EXCEEDED` for NULL handle) | Not exercised by a passing run | N/A — this run only hit the success path | **Not covered.** See below. |
| §2.3: registration is single-slot (second `add` without `remove` fails) | Not exercised — tool only calls `add` once | N/A | **Not covered.** See below. |
| OQ-3: does `remove_device_msg_listener` fully clear the registration | Not exercised | N/A | **Not covered.** See below. |
| OQ-4: delivery thread identity | Can't distinguish "same thread as caller" from "a different thread that happened to finish first" from console output alone — no thread-id logging in the existing tool | Message content was fully flushed to stdout before the next line printed, which at minimum rules out "delivered via a later async queue/poll" | **Partially informative, not conclusive.** |

## Not covered (needs a purpose-built test, not run here)

Per the user's direction, this pass used only the existing `live_capture_test`/
`scan_server` tools rather than writing new test code, so it cannot speak to:

- §2.3 / OQ-3 — double-registration-without-remove, and remove-then-add. The
  existing tool only ever calls `add_device_msg_listener` once per process
  lifetime; confirming the single-slot claim and whether `remove` truly clears it
  needs a test that calls `add`, `remove`, `add` again (and separately `add`, `add`)
  and checks the returned status codes against the spec's `845283584`
  (`OB_SCAN_ERROR_INVALID_ARGUMENT`).
- §5's NULL-handle path (`845283591` / `OB_SCAN_ERROR_DEADLINE_EXCEEDED`) — would
  need a deliberate call with a NULL handle, which the existing tool never does
  (it only ever calls with a handle already confirmed non-NULL).
- OQ-2 (listener struct shape) and OQ-7 (which binary performs the first IOKit
  registration) — both need instrumentation/tracing, not just a clean run's console
  output.

## Recommendation for `/openscanner/reconcile`

The core synchronous-delivery and data-payload claims in the PLAN #1 spec are now
hardware-confirmed, not just statically inferred. The spec's §2 and OQ-1 sections
can likely be upgraded from "[inferred]"/open to confirmed for the specific cases
exercised here. OQ-3, §2.3, and §5's NULL-handle/already-registered paths remain
genuinely open and need a dedicated `tests/hardware/*` program (builder's job per
the architecture doc) before they can be closed out.
