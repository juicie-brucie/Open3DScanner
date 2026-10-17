# Recomp plan

Single source of truth for the recon → build → hw → reconcile loop. This is a
**queue**, not a design doc: one line per task, move the status as you go.

Status: `TODO` · `RECON` · `SPEC-READY` · `BUILD` · `HW` · `DONE` · `BLOCKED`

## Active

| # | Target | Status | Notes |
| --- | --- | --- | --- |
| 1 | Device-message listener registration (add/remove + callback delivery) | DONE | Spec: [`findings/device-message-listener.md`](../recon/findings/device-message-listener.md). **HW-confirmed** (`f4c5f86`) for synchronous delivery and `data` NULL/non-NULL. The residual OQ-3, §2.3 and §5 paths are confirmed by #8's run (`2026-10-17-plan8-device-message-listener`) — OQ-3's "remove clears the slot" as the returned status code only; the `SIGSEGV` that follows the re-add is #11, not a spec claim. |
| 2 | Data- and process-message listeners before `obscan_scan_preview` | SPEC-READY | Spec: [`findings/data-process-message-listeners.md`](../recon/findings/data-process-message-listeners.md). Both slots mandatory before preview (`judge_is_exist_listener`, else `845283593`); single-slot add/remove (re-remove clears the slot, re-add OK); data = sync, drop-at-emit; process = thread-pool async enqueue. Six process ids + both `remove_*` exports read from the dylib (nothing inferred). Open: OQ-P1…P4, listener-obj shape (→ #9). |
| 3 | `obscan_scan_get_realtime_model_mesh` read-only-after-pause constraint | TODO | Partially observed; recover exact lifetime rules. |
| 4 | `obscan_scan_pause` / `stop` / `session_destroy` clean teardown (4 client ports) | TODO | Confirm all four ports released (RGB/IR/IMU/depth). |
| 5 | Header claim that the connect callback is "reproducibly" followed by a crash (and the `obscan_context_destroy` crash) | RECON | **HW-contradicted** (`f4c5f86`): worker exit 0, `destroy` SUCCESS in 807 ms, no power cycle. Narrow to exact conditions or drop "reproducibly". `2026-10-17-plan8-device-message-listener` crashed after an add/remove, but on the watcher thread in the connect callback (**#11**) — it never reached `obscan_context_destroy()`, so it neither supports nor contradicts this claim. |
| 6 | `tools/live_capture_test/main.cpp:72-74` describes detection as an "IOKit device-watcher thread" | DONE | **test wrong**: this dylib imports no IOKit; the run shows `MacPal.cpp` `Create PollingDeviceWatcher!` + `Ethernet.cpp` `NetDeviceWatcher`. Fixed in the named file. The header carried the same claim at `obscan_api.h:283-286`; #7 reworded that occurrence. |
| 7 | OQ-7 revisit: the macOS detection path | SPEC-READY | **Resolved.** Both watchers (libusb USB hotplug — VIDs `0x2BC5`/`0x1B20` — and GVCP net) start inside `obscan_context_create` and live wholly in `libOrbbecSDK.1.7.1.dylib`; `IOServiceAddMatchingNotification` is the SDK's own embedded-libusb backend, fired at `UsbContext` construction (device/upgrade transport init), never at listener registration. Header + findings reworded. Spec: [`findings/plan7-detection-path.md`](../recon/findings/plan7-detection-path.md). Follow-ups: OQ-7b/c/d. |
| 8 | Purpose-built `tests/hardware/*` for the PLAN #1 paths the existing tool never exercises | DONE | All 7 checks pass on hardware (`tests/results/2026-10-17-plan8-device-message-listener.md`): §2.3 double-add → `845283584`, OQ-3 remove-then-add → SUCCESS, §5 NULL-handle → `845283591`, OQ-4 callback thread hash == registration thread hash. **Caveat: the run was not clean** — the process died of `SIGSEGV` right after check 7, in the vendor's connect callback (#11), so the checks are confirmed while the OQ-3 path is not yet proven safe. Crash analysis and archived evidence: `tests/results/2026-10-17-plan8-device-message-listener-crash-analysis.md`. |
| 9 | OQ-2: confirm `OBScanDeviceMessageListener_` shape (single delegate at `0x00`) | RECON | Follow the monostub vtable at `__DATA_CONST 0x454d3b0`; the other listener families share the shape. |
| 10 | `device_message_listener_test` worker timeout (60 s) and the blocked doc's Vulkan rationale | DONE | Worker timeout raised to 180 s (`main.cpp:232`) and the blocked doc's Vulkan rationale corrected — additively, in `tests/results/2026-10-16-plan1-device-message-listener-test-blocked-correction.md`, leaving the original record untouched (#34). The run itself finished in ~4 s, so it did **not** exercise the new timeout — the earlier blocker simply did not recur. Neither the timeout nor the Vulkan path is implicated in the crash (#11). |
| 11 | `SIGSEGV` in the vendor's device-connected callback, after re-adding the listener (the OQ-3 path) | HW | `EXC_BAD_ACCESS` at `0x0` on the **NetDeviceWatcher/GVCP thread** (`14660873` — `crash.ips` frame 13 is `libobsensor::pal::NetDeviceWatcher::start(…)::$_1`; *not* the polling watcher, whose `Create PollingDeviceWatcher!` printed on `14660842`, `raw.log:14`), in `OBDeviceManagement::bind_device_connect_callback` → `update_camera_status` → `update_device_info` → `device::OrbbecDevice::get_active_device()`, ~60 ms after check 7's re-add returned and the `"device that you've already created"` warning — see `tests/results/2026-10-17-plan8-device-message-listener-crash-analysis.md` and the run's `.crash.ips`. **Not** the `obscan_context_destroy`/libomp crash and **not** the Vulkan hang — both retracted. Needs a controlled re-run (same sequence, crash report captured) to separate callback re-entrancy from a pre-existing inconsistency the re-add exposes; mapping the frame is recon's. |
| 12 | `tools/live_capture_test/main.cpp:70-75,369-375` says listener registration is what "starts" the vendor's device watcher, and that `get_device_num()` "stays at 0 forever" without one | TODO | **The causal half is contradicted.** PLAN #7 hw-confirms the watchers start inside `obscan_context_create`, *before* any listener exists (#7, [`findings/plan7-detection-path.md`](../recon/findings/plan7-detection-path.md) §1); the IOKit wording #6 fixed sits in the same comment block, so #6's fix left the stronger claim standing. The `get_device_num()` half is precisely what **OQ-7b** leaves unobserved — "a listener-less with-device run is still unobserved" — so it needs that run to settle, or the sentence should be dropped. Builder owns `tools/`. |
| 13 | Header's "Known hazard" block (`obscan_api.h:325-331`) attributes the connect-callback crash to the same class as the `obscan_context_destroy()`/libomp crash (vendor code assuming a full Unity/Cocoa runtime context) | DONE | **spec wrong — mechanism only.** (a) The attribution is disproved by the run's `.crash.ips`: the only images on the stack are `liborbbec_scan.3.5.3`, `libOrbbecSDK.1.7.1`, `libsystem_pthread` — no `libomp`, no `texture_mode::shutdown_network()`, no `obscan_context_destroy()` frame — and the faulting thread is the vendor **NetDeviceWatcher/GVCP** thread (`14660873`), which never ran the teardown at all. (b) The *ordering* half — "shortly after returning from `onMessage`" — is **corroborated, not disproved**, and must not be rewritten: `raw.log:75` delivers `DEVICE_CONNECT` on the worker thread, `:76` shows the `add` returning `SUCCESS`, `:78` logs `Trying to create a device that you've already created!` on `14660873` ~60 ms later, then the segfault. `onMessage` ran and returned for a real connect event. What changes is the *reading*: the crash is **not a post-return continuation of the registration call** but a concurrent fault on a different thread, from a separate `NetDeviceWatcher`-initiated device-changed callback (`crash.ips` #8–#13) whose frames run `DeviceManager::onDeviceChanged` → `OrbbecInternalDevice::deviceChangedCallback` → `bind_device_connect_callback()::$_0` → `update_camera_status` → `get_active_device`. Suggested rewording: *"the crash follows a connect callback by ~60 ms on a different thread, from a separate `NetDeviceWatcher`-initiated device-changed callback — not a post-return continuation of the registration call."* #5 covers only the word "reproducibly"; this row owns the mechanism attribution. Fixed by PR #41 (recon rewrote the header hazard block). |
| 14 | Findings §9 lists `OBDeviceManagement::bind_device_connect_callback()` as a symbol "observed without any reference", reading as a dead remnant | DONE | **spec wrong** — the `.crash.ips` faulting stack carries `std::__1::__function::__func<OBDeviceManagement::bind_device_connect_callback()::$_0, …>` (frame 4), so it is on the live hot path, not an unreferenced remnant. §9's own caveat ("'unused' is **not proven**") was the right hedge: the blind spot is precisely that §9 only looked for direct `BL`/`B` callers and address references, while this symbol is reached **indirectly** through the `std::function` indirection. Same pass: §4's "[thread identity inferred]" and OQ-4 can be advanced. The faulting thread `14660873` is the **NetDeviceWatcher/GVCP thread** — `crash.ips` frame 13 is `libobsensor::pal::NetDeviceWatcher::start(…)::$_1`, and `raw.log:30-37,42,48,78` on `14660873` are all `GVCPClient.cpp` / `Ethernet.cpp:69 NetDeviceWatcher`. Do **not** call this the "polling" watcher: per PLAN #7 the polling device watcher is a genuinely distinct object (`LibusbDeviceWatcher`, whose `Create PollingDeviceWatcher!` printed on `14660842` at context-create, `raw.log:14`), so the two names denote two different watchers, not one. Related but not identical: OQ-7d records the vendor's naming inconsistency *within* the USB-hotplug path and defers the delivery-thread question to OQ-4. Device creation and the delivered `DEVICE_CONNECT` both ran on `14660842`; OQ-4's re-entrancy half stays open (#11). |
| 15 | Findings §2/§8 + header single-slot rule treat "remove clears the slot, add succeeds" as a settled, safe re-registration | DONE | **spec gap** — the status code is a misleading safety signal. Timestamps verify, but the causal attribution does not: the create→destroy→create burst is **not** driven by check 7's `add`, it *precedes* it. In stream order, `raw.log:44` is check 5's `remove(handle, listener_a)`, then `48.745121` the NetDeviceWatcher thread logs `DeviceManager.cpp:188 Device changed! removed: 0, added: 1` (`14660873`), then creation on `14660842` — `created!` #1 `49.071709`, `destroyed!` `49.273511`, `created!` #2 `49.592673` — and only *then* `raw.log:75` the `DEVICE_CONNECT` delivery and `:76` the `add` returning `SUCCESS`. Because worker stdout and the vendor log share one stream, that ordering is reliable: the CONNECT is delivered **synchronously inside** check 7's `add`, immediately after `created!` #2, so the whole cycle had already finished before check 7 ran. What *is* ~60 ms downstream of check 7 is only the `Trying to create a device that you've already created!` + fault (`raw.log:78`). Correct reading: the burst tracks **`remove()` racing the watcher's device-change**, and the spec's single-slot rule is silent on that race. This decides #11's re-run design — **target the remove-side race, not the re-add**. |
| 16 | `tools/scan_server/main.cpp:448,454,460` holds listener objects in **stack locals** that die at `693`, after `obscan_context_destroy` at `688` | DONE | **implementation wrong — but a gap in the header rule, not a violation of it.** `obscan_api.h:309-311` only requires the listener stay alive "until it is removed **or the context is destroyed**", and these locals *are* alive through `context_destroy` at `688`, so the code obeys the rule as written. The defect is that the rule **stops at the destroy boundary** while the SDK's threads keep the pointers past it. The support is this project's own `engine/src/obscan_loader.cpp:110-117`: "It starts its own background threads … some of those outlive `obscan_context_destroy()` … keeps the listener pointers it's given". The #13/#14 crash is **not** probative evidence here — that is a NULL deref *inside vendor code*, showing the watcher acts on its own schedule, not a listener object used after free. Severity is correspondingly narrow: the exposure is the `688 → 693` window (five lines; the locals go out of scope at the closing brace) and the process exits immediately after. Structural facts all verify: locals at `448`/`454`/`460` in `RunLiveServer` (`419-693`), no `remove_*` call anywhere in `scan_server`, `context_destroy` at `688`. `live_capture_test` already fixed the broader case with file-scope statics and a comment naming the reason (`tools/live_capture_test/main.cpp:166-172`); `scan_server` was missed. Fix stands: hoist the three to file scope, and land the header-rule widening with #13/#14's pass. |
| 17 | `tests/hardware/device_message_listener_test/main.cpp` credits itself with resolving OQ-4 | TODO | **test wrong** — it measures only the registration-time delivery. `g_callback_thread` is a single global (`main.cpp:41`) overwritten at `main.cpp:47` by every message, and the thread hash is summarised exactly once (`main.cpp:147-150`), immediately after add #1, so the summary can only ever describe that one synchronous delivery. The run doc's claim that "no thread-hash was logged" for the later connect under-reads its own log: `raw.log:75` does carry `thread=17304887900616587565`. Adding that fact sharpens rather than rescues the conclusion — that hash **equals** the registration-thread hash (`raw.log:26`), so **both** observed deliveries were on `14660842` and **no watcher-thread delivery ever occurred**. Precise OQ-4 status: *confirmed for both synchronous deliveries; the async / watcher-thread case was never exercised.* Fix: record the thread id **per message** and re-summarise after the 2 s window and after the re-add, so OQ-4 can cover delivery from a vendor thread. Reconcile #8's "OQ-4 resolved" note at the same time. |

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
- **OQ-5** — Decode the `tips` / `control` / `calib` `*_name` jump tables by the
  same method (`data` and `process` families decoded by PLAN #2; feeds PLAN #3).
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

**PLAN #2 — data/process message listeners** (same slice; spec in
[`findings/data-process-message-listeners.md`](../recon/findings/data-process-message-listeners.md)):

- **OQ-P1** — process-family delivery: which thread drains the paras
  `+0x90/0x98/0xa0` queue (`{0x454dc60, this}` closures via
  `rs_common::thread_pool_enqueue_task`), and drop vs `bad_function_call` on an
  emptied slot at drain time. Runnable as a follow-up dynamic trace.
- **OQ-P2** — No lock evidence on the slot homes / queue vector: is
  add/remove/deliver racy? Needs a second-thread add/remove/re-add trace while
  frames flow.
- **OQ-P3** — `data` payload semantics per emitter (device-family OQ-1
  counterpart): `send_data_msg` passes the pointer opaque; per-stage payloads
  unmapped.
- **OQ-P4** — `0x42fa0xxx` log-label page is unmapped in both the Ghidra view
  and the file dump (identity-mapping proven elsewhere); `[+0xe1]` enable flag
  and the `[+0x4c8]` state values (`{1,3,4,5}`) unnamed.

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
- 2026-10-17 — recon (`openscanner/recon`): PLAN #2 data/process listeners
  specified → `SPEC-READY` (branch `recon/plan7-detection-path`; re-homes the
  `5835ba6` header content that `39e9156` reverted, now as a `recon:`-labelled
  finding): both-slots preview guard, single-slot add/remove, sync/async
  delivery split, six process ids decoded from the vendor name table. Opens
  OQ-P1…P4.
- 2026-10-17 — operator: PR #35 rebased onto `main` (11 commits of board drift);
  the conflict was resolved keeping `main`'s rows and taking #2/#7 `SPEC-READY`
  from the branch. Verifier follow-ups landed with it: row #1's title dropped the
  disproved "trigger for SDK device-watcher" parenthetical, #6's note updated now
  that #7 reworded the header occurrence, and **#12** filed for the contradicting
  `live_capture_test` trigger comments, which nothing was tracking.
- 2026-10-17 — reconcile: newest result
  (`2026-10-17-plan8-device-message-listener-crash-analysis.md`) compared line by
  line against `obscan_api.h` and
  [`findings/device-message-listener.md`](../recon/findings/device-message-listener.md).
  **2 spec claims contradicted** (#13 header hazard block, #14 findings §9), **1
  spec gap** (#15 re-add re-enters device creation), **1 implementation defect**
  (#16 `scan_server` stack-local listeners), **1 test defect** (#17 thread
  accounting / overstated OQ-4). No source changed in this step — tasks only.
  Evidence: the run's `.crash.ips`, `raw.log` (threads `14660842` vs `14660873`),
  and `device_message_listener_test/main.cpp`. Nothing here duplicates #11 — that
  row owns the crash itself; these own the contracts it falsified.
- 2026-10-17 — reconcile (verify pass, branch `reconcile/plan13-row-corrections`):
  verify-1's read-only pass corrected five rows. **#13(b) retracted** — the
  "upstream of `send_device_msg`" claim was wrong; the header's "shortly after
  returning from `onMessage`" ordering is *corroborated* (`raw.log:75-78`), and
  only the mechanism reading changes (concurrent watcher-thread fault, not a
  post-return continuation). **#14** main claim holds; thread relabelled
  NetDeviceWatcher/GVCP (its `std::function`-indirection point stands — that was
  §9's blind spot). **#15** timestamps verify but causality was wrong: the
  create→destroy→create burst *precedes* check 7 and tracks `remove()` racing the
  watcher, so **#11's re-run should target the remove-side race, not the
  re-add**. **#16** reframed: not a rule violation but a gap in the header rule
  at the destroy boundary, narrower severity (`688 → 693`), and the #13/#14 crash
  dropped as non-probative. **#17** holds; added that `raw.log:75`'s hash equals
  the registration hash, so no watcher-thread delivery ever occurred. Also
  relabelled the thread in **#11** and corrected the operator-authored
  crash-analysis file in place (labelled); run records untouched.
