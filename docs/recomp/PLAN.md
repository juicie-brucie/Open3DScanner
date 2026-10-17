# Recomp plan

Single source of truth for the recon → build → hw → reconcile loop. This is a
**queue**, not a design doc: one line per task, move the status as you go.

Status: `TODO` · `RECON` · `SPEC-READY` · `BUILD` · `HW` · `DONE` · `BLOCKED`

## Active

| # | Target | Status | Notes |
| --- | --- | --- | --- |
| 1 | Device-message listener registration (trigger for SDK device-watcher) | DONE | Spec: [`findings/device-message-listener.md`](../recon/findings/device-message-listener.md). **HW-confirmed** (`f4c5f86`) for synchronous delivery and `data` NULL/non-NULL. Open: OQ-3, §2.3, §5 → #8. |
| 2 | Data- and process-message listeners before `obscan_scan_preview` | TODO | Ordering requirement; preview fails otherwise. Decodable by the OQ-5 method. |
| 3 | `obscan_scan_get_realtime_model_mesh` read-only-after-pause constraint | TODO | Partially observed; recover exact lifetime rules. |
| 4 | `obscan_scan_pause` / `stop` / `session_destroy` clean teardown (4 client ports) | TODO | Confirm all four ports released (RGB/IR/IMU/depth). |
| 5 | Header claim that the connect callback is "reproducibly" followed by a crash (and the `obscan_context_destroy` crash) | RECON | **HW-contradicted** (`f4c5f86`): worker exit 0, `destroy` SUCCESS in 807 ms, no power cycle. Narrow to exact conditions or drop "reproducibly". |
| 6 | `tools/live_capture_test/main.cpp:72-74` describes detection as an "IOKit device-watcher thread" | DONE | **test wrong**: this dylib imports no IOKit; the run shows `MacPal.cpp` `Create PollingDeviceWatcher!` + `Ethernet.cpp` `NetDeviceWatcher`. Fix the comment. |
| 7 | OQ-7 revisit: the macOS detection path | RECON | `f4c5f86` shows **polling** + GVCP network discovery, no IOKit matching. Header asserts the watcher "lives in the Orbbec dylib"; findings leave it open. Reconcile the two. |
| 8 | Purpose-built `tests/hardware/*` for the PLAN #1 paths the existing tool never exercises | BUILD | §2.3 double-add, OQ-3 remove-then-add, §5 NULL-handle (`845283591`) + already-registered (`845283584`), OQ-4 thread identity. Scaffolding landed in `5835ba6`, **not yet runnable**: the launcher omits the `vulkan_disable_interpose` `DYLD_INSERT_LIBRARIES` setup `scan_server`/`live_capture_test` use — add it before running (`tests/results/2026-10-16-plan1-device-message-listener-test-blocked.md`). |
| 9 | OQ-2: confirm `OBScanDeviceMessageListener_` shape (single delegate at `0x00`) | RECON | Follow the monostub vtable at `__DATA_CONST 0x454d3b0`; the other listener families share the shape. |

## Open questions (from recon)

Recon appends unresolved items here. Each names the target and the specific unknown.

**PLAN #1 — device-message listeners** (`liborbbec_scan.3.5.3.dylib` arm64 slice,
sha256 `184d92a8…c017ec`; spec in
[`findings/device-message-listener.md`](../recon/findings/device-message-listener.md)):

- **OQ-1** — `data` payload semantics of `OBScanDeviceMessage_`. Per-call-site
  payloads are unmapped (`send_device_msg(int)` passes `this+0x167` for id `0` and
  NULL otherwise). Do not document `data` as a device list until settled.
  _Partially HW-confirmed (`f4c5f86`): NULL for the registration DISCONNECT, non-NULL
  for the real CONNECT. The literal `this+0x167` value is still unverified._
- **OQ-2** — Is `OBScanDeviceMessageListener_` really a single delegate pointer at
  `0x00`? Verify by following the monostub vtable at `__DATA_CONST 0x454d3b0`. The
  other listener families share this shape. → **#9**
- **OQ-3** — Does `remove_device_msg_listener` fully clear the stored
  `std::function`, so a later add succeeds? Not disassembled. → **#8**
- **OQ-4** — Exact thread that invokes the listener, and whether the callback can
  re-enter. Needs dynamic tracing. → **#8**
- **OQ-5** — Decode the `tips` / `control` / `data` / `process` / `calib` `*_name`
  jump tables by the same method (feeds PLAN #2 and #3).
- **OQ-6** — Is `ObScanDeviceManagementHandle` literally `OBDeviceManagement*`?
  Member offsets used (`0xb8`, `0x158`, `0xa0`) say yes; vendor names the type
  `…Handle_`. Low risk for callers, who only forward the out-parameter. → accepted
  as-is, no task.
- **OQ-7** — Reconcile "registration starts the device watcher" with the fact that
  this dylib imports no IOKit: which side performs the first
  `IOServiceAddMatchingNotification`? Candidate artifact
  `libOrbbecSDK.1.7.1.dylib` in `.recon-cache/vendor/`. → **#7** — but the HW run
  suggests the macOS path polls and uses GVCP network discovery, so this question may
  be misframed (no IOKit matching to find).

## Log

_Append dated one-liners: what changed, which agent, and the outcome._

- 2026-10-16 — recon (`openscanner/recon`): PLAN #1 device-message listener
  specified → `SPEC-READY`; findings + `obscan_api.h` in `2226a32`. Watcher located
  outside this dylib (OQ-7).
- 2026-10-16 — housekeeping (`build`): board synced to `SPEC-READY` and OQ-1…OQ-7
  filed; `.recon-cache/` re-filed (`listings/`, symlinks-only `vendor/`); snapshot
  and slice-provenance docs corrected.
- 2026-10-16 — reconcile (`build`): HW run `f4c5f86` compared against the header and
  findings. §2 synchronous delivery and OQ-1 `data` NULL/non-NULL confirmed for the
  cases exercised; **2 spec claims contradicted** (#5 crash, #7 watcher), **1 tool
  comment wrong** (#6), **1 new test program needed** (#8). PLAN #1 → `BUILD`.
