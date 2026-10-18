# Recomp plan

Single source of truth for the recon → build → hw → reconcile loop. This is a
**queue**, not a design doc: one line per task, move the status as you go.

Status: `TODO` · `RECON` · `SPEC-READY` · `BUILD` · `HW` · `DONE` · `BLOCKED`

## Active

| # | Target | Status | Notes |
| --- | --- | --- | --- |
| 1 | Device-message listener registration (add/remove + callback delivery) | DONE | Spec: [`findings/device-message-listener.md`](../recon/findings/device-message-listener.md). **HW-confirmed** (`f4c5f86`) for synchronous delivery and `data` NULL/non-NULL. The residual OQ-3, §2.3 and §5 paths are confirmed by #8's run (`2026-10-17-plan8-device-message-listener`) — OQ-3's "remove clears the slot" as the returned status code only; the `SIGSEGV` that follows the re-add is #11, not a spec claim. |
| 2 | Data- and process-message listeners before `obscan_scan_preview` | DONE | Spec: [`findings/data-process-message-listeners.md`](../recon/findings/data-process-message-listeners.md). Both slots mandatory before preview (`judge_is_exist_listener`, else `845283593`); single-slot add/remove (re-remove clears the slot, re-add OK); data = sync, drop-at-emit; process = thread-pool async enqueue. Six process ids + both `remove_*` exports read from the dylib (nothing inferred). Open: OQ-P1…P4, listener-obj shape (→ #9). **Implementation complete**: PR #46 landed the four loader exports + smoke test; PR #53 wired the data/process test behind the launcher. **Run landed**: `tests/results/2026-10-17-data-process-message-listener-test.md` (16/16 PASS) and the independent re-run `...-test-run2.md` (same 16/16, exit 0). OQ-P1…P4 remain open and are filed as #26–#29 (the roadmap's adopt-before-close). |
| 3 | `obscan_scan_get_realtime_model_mesh` read-only-after-pause constraint | TODO | Partially observed; recover exact lifetime rules. |
| 4 | `obscan_scan_pause` / `stop` / `session_destroy` clean teardown (4 client ports) | TODO | Confirm all four ports released (RGB/IR/IMU/depth). |
| 5 | Header claim that the connect callback is "reproducibly" followed by a crash (and the `obscan_context_destroy` crash) | RECON | **HW-contradicted** (`f4c5f86`): worker exit 0, `destroy` SUCCESS in 807 ms, no power cycle. Narrow to exact conditions or drop "reproducibly". `2026-10-17-plan8-device-message-listener` crashed after an add/remove, but on the watcher thread in the connect callback (**#11**) — see #20's correction: the main thread was concurrently lock-waiting *inside* `obscan_context_destroy()`, so the run neither supports nor contradicts the "reproducibly" claim. |
| 6 | `tools/live_capture_test/main.cpp:72-74` describes detection as an "IOKit device-watcher thread" | DONE | **test wrong**: this dylib imports no IOKit; the run shows `MacPal.cpp` `Create PollingDeviceWatcher!` + `Ethernet.cpp` `NetDeviceWatcher`. Fixed in the named file. The header carried the same claim at `obscan_api.h:283-286`; #7 reworded that occurrence. |
| 7 | OQ-7 revisit: the macOS detection path | BUILD | **Resolved.** Both watchers (libusb USB hotplug — VIDs `0x2BC5`/`0x1B20` — and GVCP net) start inside `obscan_context_create` and live wholly in `libOrbbecSDK.1.7.1.dylib`; `IOServiceAddMatchingNotification` is the SDK's own embedded-libusb backend, fired at `UsbContext` construction (device/upgrade transport init), never at listener registration. Header + findings reworded. Spec: [`findings/plan7-detection-path.md`](../recon/findings/plan7-detection-path.md). Follow-ups: OQ-7b/c/d. |
| 8 | Purpose-built `tests/hardware/*` for the PLAN #1 paths the existing tool never exercises | DONE | All 7 checks pass on hardware (`tests/results/2026-10-17-plan8-device-message-listener.md`): §2.3 double-add → `845283584`, OQ-3 remove-then-add → SUCCESS, §5 NULL-handle → `845283591`, OQ-4 callback thread hash == registration thread hash. **Caveat: the run was not clean** — the process died of `SIGSEGV` right after check 7, in the vendor's connect callback (#11), so the checks are confirmed while the OQ-3 path is not yet proven safe. Crash analysis and archived evidence: `tests/results/2026-10-17-plan8-device-message-listener-crash-analysis.md`. |
| 9 | OQ-2: confirm `OBScanDeviceMessageListener_` shape (single delegate at `0x00`) | DONE | **Resolved** (2026-10-17-plan9 pass) → findings §6.1; data/process byte-identical, tips/control `[inferred]`. |
| 10 | `device_message_listener_test` worker timeout (60 s) and the blocked doc's Vulkan rationale | DONE | Worker timeout raised to 180 s (`main.cpp:232`) and the blocked doc's Vulkan rationale corrected — additively, in `tests/results/2026-10-16-plan1-device-message-listener-test-blocked-correction.md`, leaving the original record untouched (#34). The run itself finished in ~4 s, so it did **not** exercise the new timeout — the earlier blocker simply did not recur. Neither the timeout nor the Vulkan path is implicated in the crash (#11). |
| 11 | `SIGSEGV` in the vendor's device-connected callback, after re-adding the listener (the OQ-3 path) | HW | `EXC_BAD_ACCESS` at `0x0` on the **NetDeviceWatcher/GVCP thread** (`14660873` — `crash.ips` frame 13 is `libobsensor::pal::NetDeviceWatcher::start(…)::$_1`; *not* the polling watcher, whose `Create PollingDeviceWatcher!` printed on `14660842`, `raw.log:14`), in `OBDeviceManagement::bind_device_connect_callback` → `update_camera_status` → `update_device_info` → `device::OrbbecDevice::get_active_device()`, ~60 ms after check 7's re-add returned and the `"device that you've already created"` warning — see `tests/results/2026-10-17-plan8-device-message-listener-crash-analysis.md` and the run's `.crash.ips`. **Not** the `obscan_context_destroy`/libomp crash and **not** the Vulkan hang — both retracted. Needs a controlled re-run (same sequence, crash report captured) to separate callback re-entrancy from a pre-existing inconsistency the re-add exposes; mapping the frame is recon's. **Controlled re-run** (`tests/results/2026-10-17-device-message-listener-test-run4.md`, 4th consecutive reproduction, identical stack): sharpens rather than settles — check 5's own remove/re-add resolves cleanly (PASS) well before the crash; the fault follows a **second, independent trailing device-changed event** racing the main thread's entry into `obscan_context_destroy()`, and #20's pairing is confirmed a 4th time (main thread lock-waiting in `stop()`). **Open**: why a device-changed callback keeps firing after the sequence completed, and whether `stop()`'s mutex is the one `get_active_device()`/`update_device_info()` need — recon's to map. |
| 12 | `tools/live_capture_test/main.cpp:70-75,369-375` says listener registration is what "starts" the vendor's device watcher, and that `get_device_num()` "stays at 0 forever" without one | DONE | **Fixed** (PR #39, `330b9f2`) — both comment blocks reworded: the contradicted causal claim is gone, and the `get_device_num()` sentence is now an explicit OQ-7b-unobserved note (OQ-7b remains open under #7). Original finding: **the causal half is contradicted.** PLAN #7 hw-confirms the watchers start inside `obscan_context_create`, *before* any listener exists (#7, [`findings/plan7-detection-path.md`](../recon/findings/plan7-detection-path.md) §1); the IOKit wording #6 fixed sits in the same comment block, so #6's fix left the stronger claim standing. The `get_device_num()` half is precisely what **OQ-7b** leaves unobserved — "a listener-less with-device run is still unobserved" — so it needs that run to settle, or the sentence should be dropped. Builder owns `tools/`. |
| 13 | Header's "Known hazard" block (`obscan_api.h:325-331`) attributes the connect-callback crash to the same class as the `obscan_context_destroy()`/libomp crash (vendor code assuming a full Unity/Cocoa runtime context) | DONE | **spec wrong — mechanism only.** (a) The attribution is disproved by the run's `.crash.ips`: the only images on the stack are `liborbbec_scan.3.5.3`, `libOrbbecSDK.1.7.1`, `libsystem_pthread` — no `libomp`, no `texture_mode::shutdown_network()`, no `obscan_context_destroy()` frame — and the faulting thread is the vendor **NetDeviceWatcher/GVCP** thread (`14660873`), which never ran the teardown at all. (b) The *ordering* half — "shortly after returning from `onMessage`" — is **corroborated, not disproved**, and must not be rewritten: `raw.log:75` delivers `DEVICE_CONNECT` on the worker thread, `:76` shows the `add` returning `SUCCESS`, `:78` logs `Trying to create a device that you've already created!` on `14660873` ~60 ms later, then the segfault. `onMessage` ran and returned for a real connect event. What changes is the *reading*: the crash is **not a post-return continuation of the registration call** but a concurrent fault on a different thread, from a separate `NetDeviceWatcher`-initiated device-changed callback (`crash.ips` #8–#13) whose frames run `DeviceManager::onDeviceChanged` → `OrbbecInternalDevice::deviceChangedCallback` → `bind_device_connect_callback()::$_0` → `update_camera_status` → `get_active_device`. Suggested rewording: *"the crash follows a connect callback by ~60 ms on a different thread, from a separate `NetDeviceWatcher`-initiated device-changed callback — not a post-return continuation of the registration call."* #5 covers only the word "reproducibly"; this row owns the mechanism attribution. Fixed by PR #41 (recon rewrote the header hazard block). |
| 14 | Findings §9 lists `OBDeviceManagement::bind_device_connect_callback()` as a symbol "observed without any reference", reading as a dead remnant | DONE | **spec wrong** — the `.crash.ips` faulting stack carries `std::__1::__function::__func<OBDeviceManagement::bind_device_connect_callback()::$_0, …>` (frame 4), so it is on the live hot path, not an unreferenced remnant. §9's own caveat ("'unused' is **not proven**") was the right hedge: the blind spot is precisely that §9 only looked for direct `BL`/`B` callers and address references, while this symbol is reached **indirectly** through the `std::function` indirection. Same pass: §4's "[thread identity inferred]" and OQ-4 can be advanced. The faulting thread `14660873` is the **NetDeviceWatcher/GVCP thread** — `crash.ips` frame 13 is `libobsensor::pal::NetDeviceWatcher::start(…)::$_1`, and `raw.log:30-37,42,48,78` on `14660873` are all `GVCPClient.cpp` / `Ethernet.cpp:69 NetDeviceWatcher`. Do **not** call this the "polling" watcher: per PLAN #7 the polling device watcher is a genuinely distinct object (`LibusbDeviceWatcher`, whose `Create PollingDeviceWatcher!` printed on `14660842` at context-create, `raw.log:14`), so the two names denote two different watchers, not one. Related but not identical: OQ-7d records the vendor's naming inconsistency *within* the USB-hotplug path and defers the delivery-thread question to OQ-4. Device creation and the delivered `DEVICE_CONNECT` both ran on `14660842`; OQ-4's re-entrancy half stays open (#11). |
| 15 | Findings §2/§8 + header single-slot rule treat "remove clears the slot, add succeeds" as a settled, safe re-registration | DONE | **spec gap** — the status code is a misleading safety signal. Timestamps verify, but the causal attribution does not: the create→destroy→create burst is **not** driven by check 7's `add`, it *precedes* it. In stream order, `raw.log:44` is check 5's `remove(handle, listener_a)`, then `48.745121` the NetDeviceWatcher thread logs `DeviceManager.cpp:188 Device changed! removed: 0, added: 1` (`14660873`), then creation on `14660842` — `created!` #1 `49.071709`, `destroyed!` `49.273511`, `created!` #2 `49.592673` — and only *then* `raw.log:75` the `DEVICE_CONNECT` delivery and `:76` the `add` returning `SUCCESS`. Because worker stdout and the vendor log share one stream, that ordering is reliable: the CONNECT is delivered **synchronously inside** check 7's `add`, immediately after `created!` #2, so the whole cycle had already finished before check 7 ran. What *is* ~60 ms downstream of check 7 is only the `Trying to create a device that you've already created!` + fault (`raw.log:78`). Correct reading: the burst tracks **`remove()` racing the watcher's device-change**, and the spec's single-slot rule is silent on that race. This decides #11's re-run design — **target the remove-side race, not the re-add**. |
| 16 | `tools/scan_server/main.cpp:448,454,460` holds listener objects in **stack locals** that die at `693`, after `obscan_context_destroy` at `688` | DONE | **implementation wrong — but a gap in the header rule, not a violation of it.** `obscan_api.h:318-320` only requires the listener stay alive "until it is removed **or the context is destroyed**", and these locals *are* alive through `context_destroy` at `688`, so the code obeys the rule as written. The defect is that the rule **stops at the destroy boundary** while the SDK's threads keep the pointers past it. The support is this project's own `engine/src/obscan_loader.cpp:110-117`: "It starts its own background threads … some of those outlive `obscan_context_destroy()` … keeps the listener pointers it's given". The #13/#14 crash is **not** probative evidence here — that is a NULL deref *inside vendor code*, showing the watcher acts on its own schedule, not a listener object used after free. Severity is correspondingly narrow: the exposure is the `688 → 693` window (five lines; the locals go out of scope at the closing brace) and the process exits immediately after. Structural facts verified at filing: listeners as locals at `448`/`454`/`460` in `RunLiveServer` (then `419-693`), no `remove_*` call anywhere in `scan_server`, `context_destroy` at `688`. Those line numbers are pre-#39 — the hoist put the listeners at file scope (`398-400`) and `RunLiveServer` is now `427-695` with `context_destroy` at `690`. The absent `remove_*` call is unchanged. `live_capture_test` already fixed the broader case with file-scope statics and a comment naming the reason (`tools/live_capture_test/main.cpp:166-172`); `scan_server` was missed. Fix stands: hoist the three to file scope, and land the header-rule widening with #13/#14's pass. **Reopened 2026-10-17** — the hoist landed (PR #39; the listeners now sit at `tools/scan_server/main.cpp:398-400`), but the second criterion did not: `obscan_api.h:318-320` still ends the requirement at "removed or the context is destroyed". Widening it is a contract edit, so it is recon's; this row sits at `BUILD` until that lands. **Fixed** (`337b34f`, landed by this PR): the rule now runs past `obscan_context_destroy()` — "keep it valid for the life of the process" — in the header's device-message block and the `add_device_msg_listener` fn comment; the findings §8/§7 bullets widened to match. Comments only (0 changed non-comment lines). |
| 17 | `tests/hardware/device_message_listener_test/main.cpp` credits itself with resolving OQ-4 | DONE | **test wrong** — it measures only the registration-time delivery. `g_callback_thread` is a single global (`main.cpp:41`) overwritten at `main.cpp:47` by every message, and the thread hash is summarised exactly once (`main.cpp:147-150`), immediately after add #1, so the summary can only ever describe that one synchronous delivery. The run doc's claim that "no thread-hash was logged" for the later connect under-reads its own log: `raw.log:75` does carry `thread=17304887900616587565`. Adding that fact sharpens rather than rescues the conclusion — that hash **equals** the registration-thread hash (`raw.log:26`), so **both** observed deliveries were on `14660842` and **no watcher-thread delivery ever occurred**. Precise OQ-4 status: *confirmed for both synchronous deliveries; the async / watcher-thread case was never exercised.* Fix: record the thread id **per message** and re-summarise after the 2 s window and after the re-add, so OQ-4 can cover delivery from a vendor thread. Reconcile #8's "OQ-4 resolved" note at the same time. **Note above describes the pre-#40 test.** Instrumentation landed in #40 — the per-phase thread lists are recorded, now read under `g_cb_mutex` through `CbCount()`/`ThreadHashList()`. Nothing further is owed in code — OQ-4 is settled by a run, so this row is `HW`. **Re-run** (`tests/results/2026-10-17-device-message-listener-test-run3.md`): a third consecutive run — all three observed deliveries again land on the registration thread; no watcher-thread delivery to the test's own listener has ever been observed. OQ-4's async half stays empirically open but is now a settled, three-times-reproduced answer; the test defect this row flagged is fixed, so the row closes. |
| 18 | OQ-2 §6.1 residuals from the PR #50 verification | DONE | Five LOW items, non-gating, for recon: (1) scope §6.1's "consumes one pointer-sized word … and nothing else" to the delivery path — `remove_device_msg_listener` is still undisassembled (OQ-3); (2) §6.1 "the invoke leaves `x2` untouched" misstates `ldr x2,[x9]; br x2` (conclusion survives — `x2` is the code pointer, not a `data` argument); (3) the `control` row's vtable cell is elided (`0x454d…`) and the tips registration site (`0x5c24d4`) appears only in prose; (4) §2.3 still says "`std::function` manager word at `this+0xb8`" where §2/§6.1 now say `__f_`; (5) range the tips/control `+0x30` invokes — the still-open part of the OQ-2 resolution. **Applied** (plan18 pass, `1d1ff17`): all five done; tips/control `+0x30` invokes disassembled byte-identical; item (1)'s premise was stale — `remove_device_msg_listener` was already listed, and the pass answered **OQ-3** (remove loads and clears `__f_`, calls the destroy slot, returns `0`; the listener argument is never read). Findings-only; no header change needed. |
| 19 | `data_process_message_listener_test` runs in-process (no worker, interposer or timeout) | DONE | Found by the PR #51 verification (item 1): no `RunSelfAsWorker`, no `vulkan_disable_interpose` wiring, no timeout, and it calls `obscan_context_destroy` (`main.cpp:156`) — `worker_process.h` documents that call can crash or hang unbounded. The PreToolUse guard now gates it to `wt-hw` alongside the device test; this row is the remaining half: wire it behind the launcher like `device_message_listener_test`. Builder owns `tests/hardware/`. **Wired** (PR #53, `071438e`): worker + interposer + 180 s timeout, mirroring the device test; `wt-hw` synced and the target rebuilt (launcher strings verified in the binary). **Run landed**: `tests/results/2026-10-17-data-process-message-listener-test.md` — 16/16 PASS, clean exit, launcher banner from `wt-hw`'s interposer; re-confirmed by `...-test-run2.md`. |
| 20 | Archived crash reports show the main thread inside `obscan_context_destroy` when the watcher thread faults | DONE | Raised by the #56 verification (observation 2): with the reports now on `main`, all three archived crash reports show the main thread in `OBDeviceManagement::release()` → `~OrbbecDevice` → `stop()` on a mutex while the watcher thread faults. That contradicts row #5's "it never reached `obscan_context_destroy()`" and the crash analysis's line 56; #11/#15's re-run design depends on it. **Confirmed** across all three archived reports (thread 0 identically `obscan_context_destroy` → `OBDeviceManagement::release()` → `~OrbbecDevice` → `~OrbbecInternalDevice` → `stop()`, blocked on `recursive_mutex::lock()`, while thread 5 faults). Row #5's "it never reached `obscan_context_destroy()`" is **contradicted** — dropped. The crash analysis's line 56 stays true of the *faulting* thread but overreached: a different thread (main) was concurrently inside the teardown, lock-waiting on the device being destroyed. Correction: [`...-crash-reports-main-thread-teardown-correction.md`](../../tests/results/2026-10-17-crash-reports-main-thread-teardown-correction.md). Mutex identity stays open — feeds #11's re-run. |
| 21 | New run records and raw logs under `tests/results/**` are script-tier, so a fabricated record would auto-merge | TODO | Raised by the #58 verification (M4 residual): `run-records` guards modification, deletion and renaming of existing records, but an added record is script-tier — `verdict-gate` auto-sets `verdict` with no judgment pass. Records are the highest-stakes artifact class; decide between moving added records/logs to the agent tier or adding a provenance check (e.g. a required companion raw log whose command line matches the record). |
| 22 | Launcher banners were printed without flushing, so piped captures showed them late | DONE | Found by the PR #53 verification (finding 3) and re-flagged in every hardware record's banner check: the `[launcher]` banners used `printf` with no `fflush`, so the worker's flushed lines preceded them in piped output. Fixed in PR #62 for both hardware tests, `live_capture_test` and `smoke_test` (the `Running …` banners too); the banner check should now pass literally on the next hardware run. |
| 23 | Live per-frame data: `obscan_scan_get_raw_cloud` returns 0 points during an active scan — map its native implementation | SPEC-READY | Raised from Claude's manual run (`tests/results/2026-10-17-live-scan-manual-test.md`): 0 points on all 3000 ticks; the only geometry is the post-pause `get_realtime_model_mesh` snapshot (5845 pts / 4167 tris). README's "never observed nonzero … regardless of config" is an observation about our usage, not a recovered fact — the vendor app streams live. Map `obscan_scan_get_raw_cloud`'s native body: what it reads, what gates the count (buffer / ready flag / mode / pause), and where that state is written from. Deliverable: the first findings doc for the scan family. Follow-ups: the scan API surface beyond the header subset; config/mode flags; the `data` payloads (OQ-1/OQ-P3) as a possible frame channel. **Answered** (plan23 pass, `8cf2220`): the gate is a **publication trigger, not config or hardware** — the cloud is double-buffered and the temp→show swap is called only from the color-cloud getter (`obscan_scan_get_model_point_color_clouds`, vtable `+0xe8`); polling `get_raw_cloud` alone never publishes, so the show cloud stays empty after the scan-start clear. Findings: [`scan-live-data.md`](../recon/findings/scan-live-data.md). → `SPEC-READY`; #24/#25 filed. |
| 24 | Add the color-cloud getter (+ release) to the header subset | DONE | From #23's answer: `obscan_scan_get_model_point_color_clouds` (vtable `+0xe8`) is the call that publishes the cloud, plus `model_clouds_release`; document the §3 publish-trigger semantics and the `+0x4c8` state the getter requires (OQ-S1). Evidence: findings §3. **Landed** (`24dba29`, this PR): `obscan_scan_get_model_point_color_clouds` (4-arg), `model_clouds_release` (a no-op `ret`), and `ObScanCloudBbox` (48 bytes, pads explicit) in the header; the publish-trigger, the step gate 1..9, the unvalidated `out_count` trap and the borrowed-pointer rules documented. **OQ-S1 answered** (findings §5): `scan+0x4c8` is the `OrbbecScanStep` machine; an active scan sits at 2, inside the getter's window. |
| 25 | Poll `obscan_scan_get_model_point_color_clouds` per tick in `scan_server` so the live view publishes | DONE | From #23's answer: loader export + per-tick call so the live view gets points during scanning. Acceptance: a hardware run shows live points while scanning (OQ-S5). **Unblocked**: #24 landed (`24dba29`). **Landed** (PR #67, `a5a78e7`): the loader resolves both symbols; `scan_server` polls the color getter per tick and broadcasts the published cloud. **OQ-S5 resolved** (`tests/results/2026-10-17-scan-server-live-acceptance.md`, exit 0, clean teardown): `color_cloud num_points` goes nonzero (292 → 883, growing) from tick 50/300 onward, every sampled tick with `paused=0` — live geometry now publishes during active scanning, not just at the final post-pause mesh. No symbol-resolution failure for either export at load. **New, unexplained**: `raw_cloud num_points` tracked `color_cloud num_points` almost exactly at every sampled tick in this run, contradicting #23's characterization of `raw_cloud` staying at 0 during active scanning — filed as **#30**. |
| 26 | OQ-P1 — which thread drains the process-family paras queue; drop vs `bad_function_call` on an emptied slot | RECON | From the roadmap's OQ adoption (the OQ list under PLAN #2): a follow-up dynamic trace. |
| 27 | OQ-P2 — are the slot homes / queue vector racy (no lock evidence)? | RECON | From the roadmap's OQ adoption: a second-thread add/remove/re-add trace while frames flow. |
| 28 | OQ-P3 — `data` payload semantics per emitter (`send_data_msg` passes the pointer opaque) | RECON | From the roadmap's OQ adoption — **doubly load-bearing**: it gates the listener live path (the research corpus's §1.5 claim) and is now also the live-path decision's **revisit trigger**: if `FRAME_READY`'s `data` carries a consumable cloud, re-take the decision (findings §6). |
| 29 | OQ-P4 — the `0x42fa0xxx` log-label page; the `[+0xe1]` enable flag and `[+0x4c8]` state values | RECON | From the roadmap's OQ adoption. |
| 30 | `obscan_scan_get_raw_cloud` returned nonzero, in lockstep with `obscan_scan_get_model_point_color_clouds`, during #25's OQ-S5 acceptance run | RECON | PLAN #23 found (findings: [`scan-live-data.md`](../recon/findings/scan-live-data.md)) that polling `get_raw_cloud` alone never publishes during active scanning — only the color-cloud getter does, via a double-buffer swap only that call triggers. #25's acceptance run (`tests/results/2026-10-17-scan-server-live-acceptance.md`) shows `raw_cloud num_points` tracking `color_cloud num_points` almost exactly at every sampled tick (e.g. both 292 at tick 50, both 883 at tick 250) — not the flat 0 #23 documented. Candidate explanations, unordered: (a) the color-getter's publish swap also flips whatever flag/buffer `get_raw_cloud` reads, so polling color first each tick incidentally unblocks raw too; (b) `scan_server`'s per-tick call order (color before raw, or vice versa) differs from whatever #23's original probe used; (c) a vendor-side state change between the dylib version #23 probed and this run's. Needs a trace of `get_raw_cloud`'s native body (as #23 did for the color getter) and/or an isolated run that polls only `get_raw_cloud` (no color-getter call) to see if it alone still returns 0. |

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
- **OQ-2** — *Resolved* (2026-10-17-plan9 pass) → findings §6.1. → **#9**
- **OQ-3** — *Resolved* (plan18 pass) → findings §6.1: remove loads and clears
  `__f_`, calls the destroy slot, returns `0`; the listener argument is never
  read; add-after-remove `[hw-confirmed: 2026-10-17-plan8-device-message-listener]`. → **#8**
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
  emptied slot at drain time. Runnable as a follow-up dynamic trace. → **#26**
- **OQ-P2** — No lock evidence on the slot homes / queue vector: is
  add/remove/deliver racy? Needs a second-thread add/remove/re-add trace while
  frames flow. → **#27**
- **OQ-P3** — `data` payload semantics per emitter (device-family OQ-1
  counterpart): `send_data_msg` passes the pointer opaque; per-stage payloads
  unmapped. → **#28**
- **OQ-P4** — `0x42fa0xxx` log-label page is unmapped in both the Ghidra view
  and the file dump (identity-mapping proven elsewhere); `[+0xe1]` enable flag
  and the `[+0x4c8]` state values (`{1,3,4,5}`) unnamed. → **#29**

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
- 2026-10-17 — repair: PR #40 (OQ-4 thread accounting) was merged carrying
  verify-1's two MEDIUMs unfixed, and PR #44 had marked **#16** `DONE` with half
  its criteria unmet — the file-scope hoist landed, but the header-rule widening
  never did, and the rule still stops at the destroy boundary. Fixed here:
  `#include <mutex>` and a locked `CbCount()` accessor replace the four unguarded
  `g_cb_threads.size()` reads that raced the locked `push_back`; **#16 reopens to
  `BUILD`** (the widening is a contract edit, recon's); **#17 moves to `HW`** (its
  instrumentation landed, so a run is what settles OQ-4). ARCHITECTURE.md restates
  the landing gate — a merge needs a verifier verdict on the exact head commit, and
  the verifier is a reporter, never an executor — and its Git-discipline item now adds
  that a room is only ever torn down by the user.
- 2026-10-17 — dispatcher: landed `recon/round4`'s OQ-2 resolution (the plan9
  pass, findings §6.1) — the superseded round6 status block was dropped. #9 and
  #12 → `DONE`. Nothing else was unmerged: `recon/round5` is subsumed by §6.1,
  and `backup/recon-plan7-2026-10-17` holds only pre-fix wording.
- 2026-10-17 — dispatcher: PR #50 merged on verifier verdict `MERGEABLE`
  (`28a9c7d`) — `recon/round4`'s OQ-2 §6.1 landed, #9/#12 → `DONE`. Redundant
  local branches `recon/round4`/`recon/round5` deleted after byte-check against
  `main`; `backup/recon-plan7-2026-10-17` kept (superseded — only pre-fix
  wording).
- 2026-10-17 — dispatcher: hardware guardrail paths updated `wt-build1` →
  `wt-hw` (`CLAUDE.md`, `.claude/hooks/guard-hw-run.sh`); the old worktree no
  longer exists. Row #18 filed for the OQ-2 §6.1 residuals.
- 2026-10-17 — dispatcher: guard now covers both listener tests
  (`*message_listener_test*`); row #19 filed for the `data_process_message_listener_test`
  launcher wiring (PR #51 verification item 1).
- 2026-10-17 — dispatcher: row #16's lifetime-rule widening landed (recon
  `337b34f`); #16 → `DONE`, #2/#19 → `HW` (implementation complete; the
  data/process test's first hardware run is the next evidence). `wt-hw` synced
  to main and the target rebuilt.
- 2026-10-17 — dispatcher: crash-report gap closed — the two
  `device_message_listener_test` crash reports left in
  `~/Library/Logs/DiagnosticReports/` are archived into `tests/results/`, with
  dated corrections to the two records that said none existed; hw-test rule 11
  now requires the check on every non-clean exit.
- 2026-10-17 — repair: PR #55 was merged by the operator while its verification
  was still in flight; the verifier's verdict (`NEEDS CHANGE` — the corrections
  overclaimed) landed after the merge. Fixed forward: both corrections reworded;
  rule 11 extended to reports found after the record was written.
- 2026-10-17 — dispatcher: verification rebuilt — `scripts/verify-pr.py`
  (build, records-in-place, plan syntax, archival hashes, hook invariants) runs
  as the CI `verify-pr` status from a trusted copy on `main`; `verdict-gate`
  sets `verdict` automatically for script-tier PRs and from a verifier's
  anchored `VERDICT: ... @ <sha>` comment for agent-tier PRs; `main` gets
  branch protection + auto-merge. Agent tier: code, `.github/`, `.claude/`,
  `docs/recon/`, and `tests/results/**` addenda.

- 2026-10-17 — recon: row #18 applied (plan18 pass) — five §6.1/§2.3 corrections,
  tips/control `+0x30` invokes ranged byte-identical, OQ-3 answered; #18 → `DONE`.

- 2026-10-17 — build: launcher banners now flush before worker output (PR #62) —
  fixes the banner-position anomaly every hardware record had to explain;
  #22 → `DONE`.

- 2026-10-17 — dispatcher: Claude's ad hoc live-scan run landed as a record
  (`tests/results/2026-10-17-live-scan-manual-test.md`); row #23 filed for the
  live per-frame path (`obscan_scan_get_raw_cloud` = 0 points during scanning).

- 2026-10-17 — recon: row #23 answered (plan23 pass) — the live-path gate is the
  color-cloud getter's publish trigger, not config or hardware; findings
  `scan-live-data.md`; #23 → `SPEC-READY`; #24/#25 filed (contract + live poll).

- 2026-10-17 — recon: row #24 landed (plan24 pass) — the color getter + release
  + `ObScanCloudBbox` in the header; OQ-S1 answered; #24 → `DONE`; #25 unblocked
  for the builder.

- 2026-10-17 — build: row #25 landed (PR #67) — the per-tick color-getter poll is
  wired into `scan_server`; #25 → `HW` (acceptance run OQ-S5).

- 2026-10-17 — dispatcher: consumed the planner's rev-5 roadmap — landed
  `docs/recomp/ROADMAP.md`; reconciled #2/#19 against the landed data-process
  record (both `DONE`); OQ-P1…P4 filed as #26–#29; the roadmap's live-path
  decision, the listener-drain row and the G1–G7 goals stay captured there.

- 2026-10-18 — fix: gate hole — a skipped job's check run satisfied the required
  `verdict` context, so agent-tier PRs could auto-merge without a verdict
  (proven by #67's fix head `a88d576`). The `verdict` job is renamed
  `verdict-comment`; the context is the status only. #67's fix head is being
  read post-merge for the record.

- 2026-10-17 — hardware (`hw-test`, `wt-hw`): `data_process_message_listener_test`
  re-run — exit 0, 16/16 PASS, launcher banner present; second independent clean
  run (#2/#19 notes cite it).
- 2026-10-17 — hardware (`wt-hw`): `device_message_listener_test` run 3 for #17 —
  all 7 checks PASS, SIGSEGV in teardown (3rd reproduction of #11); all deliveries
  again on the registration thread; #17 → `DONE`.
- 2026-10-17 — reconcile: row #20 re-read — all three archived `.crash.ips` show
  thread 0 lock-waiting in `stop()` inside `obscan_context_destroy()` while the
  watcher thread faults; row #5's "never reached destroy" retracted; #20 → `DONE`.
- 2026-10-17 — hardware (`wt-hw`): `device_message_listener_test` run 4 — the #11
  controlled re-run: 4th consecutive reproduction; refined reading (trailing
  device-changed event races the main thread's entry into `obscan_context_destroy`).

- 2026-10-17 — recon: the live-path decision (roadmap rev-5 seed #1) — getter-publish
  is ours (validates #25); the listener is not the live-view path but stays a
  mandatory registration worth draining (G6), with #28 (OQ-P3) as its use gate and
  the decision's revisit trigger. Findings `scan-live-data.md` §6.
- 2026-10-17 — hardware (`hw-test`, `wt-hw`, branch `hw/next`): #25's OQ-S5
  acceptance run — `scan_server --live` for a 30s session, exit 0, clean
  pause→mesh→stop→destroy teardown. `color_cloud num_points` goes nonzero
  (292→883, growing) from tick 50/300 onward with `paused=0` throughout —
  live points publish during active scanning. #25 → `DONE`. Unexpected:
  `raw_cloud num_points` tracked `color_cloud` in lockstep this run,
  contradicting #23's "always 0 during scanning" — filed as **#30**.
