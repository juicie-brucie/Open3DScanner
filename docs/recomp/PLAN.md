# Recomp plan

Single source of truth for the recon → build → hw → reconcile loop. This is a
**queue**, not a design doc: one line per task, move the status as you go.

Status: `TODO` · `RECON` · `SPEC-READY` · `BUILD` · `HW` · `DONE` · `BLOCKED`

## Active

| # | Target | Status | Notes |
| --- | --- | --- | --- |
| 1 | Device-message listener registration (trigger for SDK device-watcher) | DONE | Spec: [`findings/device-message-listener.md`](../recon/findings/device-message-listener.md). **HW-confirmed** (`f4c5f86`) for synchronous delivery and `data` NULL/non-NULL. The residual OQ-3, §2.3 and §5 paths are confirmed by #8's run (`2026-10-17-plan8-device-message-listener`) — OQ-3's "remove clears the slot" as the returned status code only; the `SIGSEGV` that follows the re-add is #11, not a spec claim. |
| 2 | Data- and process-message listeners before `obscan_scan_preview` | TODO | Ordering requirement; preview fails otherwise. Decodable by the OQ-5 method. |
| 3 | `obscan_scan_get_realtime_model_mesh` read-only-after-pause constraint | TODO | Partially observed; recover exact lifetime rules. |
| 4 | `obscan_scan_pause` / `stop` / `session_destroy` clean teardown (4 client ports) | TODO | Confirm all four ports released (RGB/IR/IMU/depth). |
| 5 | Header claim that the connect callback is "reproducibly" followed by a crash (and the `obscan_context_destroy` crash) | RECON | **HW-contradicted** (`f4c5f86`): worker exit 0, `destroy` SUCCESS in 807 ms, no power cycle. Narrow to exact conditions or drop "reproducibly". `2026-10-17-plan8-device-message-listener` crashed after an add/remove, but on the watcher thread in the connect callback (**#11**) — it never reached `obscan_context_destroy()`, so it neither supports nor contradicts this claim. |
| 6 | `tools/live_capture_test/main.cpp:72-74` describes detection as an "IOKit device-watcher thread" | DONE | **test wrong**: this dylib imports no IOKit; the run shows `MacPal.cpp` `Create PollingDeviceWatcher!` + `Ethernet.cpp` `NetDeviceWatcher`. Fixed in the named file. The header still carries the same claim at `obscan_api.h:283-286` — that occurrence is #7's scope. |
| 7 | OQ-7 revisit: the macOS detection path | SPEC-READY | **Resolved.** Both watchers (libusb USB hotplug — VIDs `0x2BC5`/`0x1B20` — and GVCP net) start inside `obscan_context_create` and live wholly in `libOrbbecSDK.1.7.1.dylib`; `IOServiceAddMatchingNotification` is the SDK's own embedded-libusb backend, fired at `UsbContext` construction (device/upgrade transport init), never at listener registration. Header + findings reworded. Spec: [`findings/plan7-detection-path.md`](../recon/findings/plan7-detection-path.md). Follow-ups: OQ-7b/c/d. |
| 8 | Purpose-built `tests/hardware/*` for the PLAN #1 paths the existing tool never exercises | DONE | All 7 checks pass on hardware (`tests/results/2026-10-17-plan8-device-message-listener.md`): §2.3 double-add → `845283584`, OQ-3 remove-then-add → SUCCESS, §5 NULL-handle → `845283591`, OQ-4 callback thread hash == registration thread hash. **Caveat: the run was not clean** — the process died of `SIGSEGV` right after check 7, in the vendor's connect callback (#11), so the checks are confirmed while the OQ-3 path is not yet proven safe. Crash analysis and archived evidence: `tests/results/2026-10-17-plan8-device-message-listener-crash-analysis.md`. |
| 9 | OQ-2: confirm `OBScanDeviceMessageListener_` shape (single delegate at `0x00`) | RECON | Follow the monostub vtable at `__DATA_CONST 0x454d3b0`; the other listener families share the shape. |
| 10 | `device_message_listener_test` worker timeout (60 s) and the blocked doc's Vulkan rationale | DONE | Worker timeout raised to 180 s (`main.cpp:232`) and the blocked doc's Vulkan rationale corrected — additively, in `tests/results/2026-10-16-plan1-device-message-listener-test-blocked-correction.md`, leaving the original record untouched (#34). The run itself finished in ~4 s, so it did **not** exercise the new timeout — the earlier blocker simply did not recur. Neither the timeout nor the Vulkan path is implicated in the crash (#11). |
| 11 | `SIGSEGV` in the vendor's device-connected callback, after re-adding the listener (the OQ-3 path) | HW | `EXC_BAD_ACCESS` at `0x0` on the polling device-watcher thread, in `OBDeviceManagement::bind_device_connect_callback` → `update_camera_status` → `update_device_info` → `device::OrbbecDevice::get_active_device()`, immediately after check 7's re-add and the `"device that you've already created"` warning — see `tests/results/2026-10-17-plan8-device-message-listener-crash-analysis.md` and the run's `.crash.ips`. **Not** the `obscan_context_destroy`/libomp crash and **not** the Vulkan hang — both retracted. Needs a controlled re-run (same sequence, crash report captured) to separate callback re-entrancy from a pre-existing inconsistency the re-add exposes; mapping the frame is recon's. |

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
  `libOrbbecSDK.1.7.1.dylib` in `.recon-cache/vendor/`. → **#7** — **resolved**: watchers (libusb hotplug
  VIDs `0x2BC5`/`0x1B20` + GVCP net) are constructed inside
  `obscan_context_create` and live wholly in `libOrbbecSDK.1.7.1.dylib`; the first
  `IOServiceAddMatchingNotification` is that dylib's embedded-libusb darwin
  backend at `UsbContext` construction. Follow-ups OQ-7b/c/d in
  [`findings/plan7-detection-path.md`](../recon/findings/plan7-detection-path.md).

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
- 2026-10-17 — hardware (Claude Code, `wt-build1` → `hw/plan8-plan10`): PLAN #8's run
  landed — **all 7 checks pass**; #1, #8, #10 → `DONE`. The run's `SIGSEGV` was first
  written up as the known libomp crash in `obscan_context_destroy()`, then **retracted**:
  the crash report (archived with the run) shows a NULL deref in the vendor's
  connect-callback on the watcher thread, right after the OQ-3 re-add → new **#11**.
- 2026-10-17 — housekeeping (`build`): PR #34 split — hardware run records and the
  Claude Code guardrails moved off the build PR onto `hw/plan8-plan10` and
  `chore/claude-hw-guardrails`; #34 is build-only again. Run artefacts (crash report,
  vendor `scan_log`, SDK log window) archived under `tests/results/` instead of being
  left in `/tmp` and `~/Library/Logs/DiagnosticReports`.
- 2026-10-17 — recon (`openscanner/recon`): PLAN #7 OQ-7 resolved → `SPEC-READY`
  (branch `recon/plan7-detection-path`): detection = SDK-dylib libusb hotplug
  (VIDs `0x2BC5`/`0x1B20`) + GVCP net watcher, both started at
  `obscan_context_create`; IOKit matching belongs to the embedded-libusb darwin
  backend at `UsbContext` construction. Header + findings reworded.
