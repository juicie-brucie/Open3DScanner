# Recomp plan

Single source of truth for the recon → build → hw → reconcile loop. This is a
**queue**, not a design doc: one line per task, move the status as you go.

Status: `TODO` · `RECON` · `SPEC-READY` · `BUILD` · `HW` · `DONE` · `BLOCKED`

## Active

| # | Target | Status | Notes |
| --- | --- | --- | --- |
| 1 | Device-message listener registration (add/remove + callback delivery) | DONE | **HW-confirmed** (`f4c5f86`) · #8 · #11 |
| 2 | Data- and process-message listeners before `obscan_scan_preview` | DONE | Spec: · 845283593 · #9 · #46 · #53 · tests/results/2026-10-17-data-process-message-listener-test.md · #26 · #29 |
| 3 | `obscan_scan_get_realtime_model_mesh` read-only-after-pause constraint | DONE | **Answered** (`e22510a`, this PR) |
| 4 | `obscan_scan_pause` / `stop` / `session_destroy` clean teardown (4 client ports) | DONE | **Answered** (`4e6aadd`, this PR) |
| 5 | Header claim that the connect callback is "reproducibly" followed by a crash (and the `obscan_context_destroy` crash) | DONE | **HW-contradicted** (`f4c5f86`) · #11 · #20 |
| 6 | `tools/live_capture_test/main.cpp:72-74` describes detection as an "IOKit device-watcher thread" | DONE | **test wrong** — the dylib imports no IOKit; the run shows `MacPal.cpp` + `Ethernet.cpp` (the header's claim reworded in #7). |
| 7 | OQ-7 revisit: the macOS detection path | DONE | **Resolved.** Both watchers (libusb USB hotplug — VIDs `0x2BC5`/`0x1B20` — and GVCP net) start inside `obscan_context_create` and live wholly in `libOrbbecSDK.1.7.1.dylib`; |
| 8 | Purpose-built `tests/hardware/*` for the PLAN #1 paths the existing tool never exercises | DONE | All 7 checks pass on hardware (`tests/results/2026-10-17-plan8-device-message-listener.md`): · 845283584 · 845283591 · #11 · tests/results/2026-10-17-plan8-device-message-listener-crash-analysis.md |
| 9 | OQ-2: confirm `OBScanDeviceMessageListener_` shape (single delegate at `0x00`) | DONE | **Resolved** (2026-10-17-plan9 pass) → findings §6.1; |
| 10 | `device_message_listener_test` worker timeout (60 s) and the blocked doc's Vulkan rationale | DONE | **Fixed** — worker timeout 180 s (`main.cpp:232`); the doc's Vulkan rationale corrected additively. · tests/results/2026-10-16-plan1-device-message-listener-test-blocked-correction.md · #34 · #11 |
| 11 | `SIGSEGV` in the vendor's device-connected callback, after re-adding the listener (the OQ-3 path) | DONE | **HW-contradicted** — `EXC_BAD_ACCESS` on the NetDeviceWatcher/GVCP thread, not the polling watcher. · 14660873 · 14660842 · tests/results/2026-10-17-plan8-device-message-listener-crash-analysis.md · tests/results/2026-10-17-device-message-listener-test-run4.md · #20 · #0 |
| 12 | `tools/live_capture_test/main.cpp:70-75,369-375` says listener registration is what "starts" the vendor's device watcher, and that `get_device_num()` "stays at 0 forever" without one | DONE | **Fixed** (PR #39, `330b9f2`) · #7 · #6 |
| 13 | Header's "Known hazard" block (`obscan_api.h:325-331`) attributes the connect-callback crash to the same class as the `obscan_context_destroy()`/libomp crash (vendor code assuming a full Unity/Cocoa runtime context) | DONE | **spec wrong — mechanism only.** (a) The attribution is disproved by the run's `.crash.ips`: · 14660873 · #8 · #13 · #5 · #41 |
| 14 | Findings §9 lists `OBDeviceManagement::bind_device_connect_callback()` as a symbol "observed without any reference", reading as a dead remnant | DONE | **Spec wrong** — the `.crash.ips` faulting stack carries the `bind_device_connect_callback` function object; the symbol is live. · 14660873 · #7 · 14660842 · #11 |
| 15 | Findings §2/§8 + header single-slot rule treat "remove clears the slot, add succeeds" as a settled, safe re-registration | DONE | **spec gap** — the status code is a misleading safety signal. · 14660873 · 14660842 · #1 · #2 · #11 |
| 16 | `tools/scan_server/main.cpp:448,454,460` holds listener objects in **stack locals** that die at `693`, after `obscan_context_destroy` at `688` | DONE | **Fixed** (`337b34f`, landed by this PR) · #13 · #14 · #39 |
| 17 | `tests/hardware/device_message_listener_test/main.cpp` credits itself with resolving OQ-4 | DONE | **Re-run** (`tests/results/2026-10-17-device-message-listener-test-run3.md`) · #1 · 14660842 · #8 · #40 |
| 18 | OQ-2 §6.1 residuals from the PR #50 verification | DONE | **Applied** (plan18 pass, `1d1ff17`) |
| 19 | `data_process_message_listener_test` runs in-process (no worker, interposer or timeout) | DONE | **Wired** (PR #53, `071438e`) · #51 · tests/results/2026-10-17-data-process-message-listener-test.md |
| 20 | Archived crash reports show the main thread inside `obscan_context_destroy` when the watcher thread faults | DONE | Raised by the #56 verification (observation 2): · #11 · #15 · tests/results/2026-10-17-crash-reports-main-thread-teardown-correction.md |
| 21 | New run records and raw logs under `tests/results/**` are script-tier, so a fabricated record would auto-merge | DONE | Raised by the #58 verification (M4 residual): · tests/results/** · #143 · #145 · #162 |
| 22 | Launcher banners were printed without flushing, so piped captures showed them late | DONE | Found by the PR #53 verification (finding 3) and re-flagged in every hardware record's banner check: · #62 |
| 23 | Live per-frame data: `obscan_scan_get_raw_cloud` returns 0 points during an active scan — map its native implementation | DONE | **Answered** (plan23 pass, `8cf2220`) · tests/results/2026-10-17-live-scan-manual-test.md · #24 · #25 |
| 24 | Add the color-cloud getter (+ release) to the header subset | DONE | **Landed** (`24dba29`, this PR) · #23 |
| 25 | Poll `obscan_scan_get_model_point_color_clouds` per tick in `scan_server` so the live view publishes | DONE | **Landed** (PR #67, `a5a78e7`) · **OQ-S5 resolved** (`tests/results/2026-10-17-scan-server-live-acceptance.md`, exit 0, clean teardown) · #23 · #24 · 24dba29 · #30 |
| 26 | OQ-P1 — which thread drains the process-family paras queue; drop vs `bad_function_call` on an emptied slot | DONE | **Answered** (`a2e3355`, this PR) · #2 |
| 27 | OQ-P2 — are the slot homes / queue vector racy (no lock evidence)? | DONE | **Answered** (`d7c4c63`, this PR) |
| 28 | OQ-P3 — `data` payload semantics per emitter (`send_data_msg` passes the pointer opaque) | DONE | **Answered** (`ac7654a`, this PR) · tests/results/2026-10-18-scan-server-data-process-instrumentation.md · 325189888 · 325189889 · 325189890 · #34 |
| 29 | OQ-P4 — the `0x42fa0xxx` log-label page; the `[+0xe1]` enable flag and `[+0x4c8]` state values | DONE | **Answered** (`aa25669`, this PR) |
| 30 | `obscan_scan_get_raw_cloud` returned nonzero, in lockstep with `obscan_scan_get_model_point_color_clouds`, during #25's OQ-S5 acceptance run | DONE | **Answered** (`900f9bb`, this PR) · #23 · #25 · tests/results/2026-10-17-scan-server-live-acceptance.md |
| 31 | Instrument the data/process listeners so the log records what the SDK actually delivers | DONE | **Landed** (PR #74, `dfab0ce`) · **Run landed** (`tests/results/2026-10-18-scan-server-data-process-instrumentation.md`) · #28 |
| 32 | The live view stalls at scale: per-tick cost grows unbounded with accumulated points | DONE | **Landed** (PR #77, `9cd0854`) · **Run 1** (`tests/results/2026-10-18-scan-server-scale-acceptance.md` + its operator note) · **Acceptance MET** (`tests/results/2026-10-18-scan-server-scale-acceptance-rerun.md`) · **Re-confirmed at scale** (`tests/results/2026-10-18-scan-server-scale-teardown-acceptance.md`) · tests/results/2026-10-17-plan25-manual-rerun.md · #30 · #37 |
| 33 | `pkill -f "scan_server --live"` kills only the launcher — the `--live-worker` child survives holding the port and the device | DONE | **Fixed** (PR #77) |
| 34 | The frame API (`obscan_frame_*`) is absent from the header subset — the prerequisite for the listener live path | DONE | **Answered** — `FRAME_READY`'s payload layout (points `impl+0xa0`, count `+0x100`, …). · #28 · ac7654a · #24 · #36 |
| 35 | The header declares `obscan_scan_realtime_model_mesh_release`, but the loader does not resolve it | DONE | **Landed** (PR #94, `f37b76f`) · #84 |
| 36 | OQ-P5 — the five direct-out getters' caller-allocated struct layouts | DONE | **Answered** — the five caller-allocated getters' layouts (OQ-P5); header declarations landed. · #34 |
| 37 | The teardown hangs after `rotate pose for model.ply` at a large mesh — the launcher's 420 s watchdog force-kills the worker (exit 2) | DONE | **Answered** (`b6753e8`, PR #106) · #32 · tests/results/2026-10-18-scan-server-scale-acceptance-rerun.md · #43 |
| 38 | The final-mesh broadcast is uncapped — one 15.6M-point frame (~187 MB on the wire) and the HUD dips to 29.2 fps rendering it | DONE | **Landed** (PR #93, `a6d7322`) · #32 · #39 |
| 39 | The final-mesh broadcast is capped at 20k, so the UI's final model is a preview — add a basic full-detail path | DONE | **Landed** (PR #95, `61fa31b`) · #38 · #32 |
| 40 | V4's blocker: `get_global_pose` / `get_markers` are not bound in the header (the scanner-POV pose channel) | DONE | **Answered** (`70c4e0a`, PR #106) · **landed** (PR #121) |
| 41 | V2's remainder: client backpressure for the frame stream (the server-side decimation landed as #32) | DONE | **Landed** (PR #109, `9b5645b`) · #32 · #77 · #54 · #138 · b46662b · #56 |
| 42 | V1 — the frame protocol: a versioned envelope + typed messages, with a codec on both ends (the viewer family's foundation) | DONE | **Landed** (PR #104, `1526356`) · **Hardware-confirmed** (`tests/results/2026-10-18-scan-server-v1-protocol-smoke-test.md`) · #39 · #40 · #41 · docs/architecture/scan-viewer.md · #45 |
| 43 | The teardown can outlast the launcher's watchdog when the SDK's out-of-core serialization runs long — decide the tool-side bound | DONE | **Landed** (PR #111, `ecd8798`) · **Acceptance evidenced** (`tests/results/2026-10-18-scan-server-backpressure-follow-smoke.md`) · #37 · #43 · #46 |
| 44 | The pose/marker residuals (OQ-S7/S8/S9/S11) — the pose convention, the marker field names, the borrow stability, the marker-mesh signature | DONE | **Landed** (PR #119) · #40 |
| 46 | The destroy refusals aren't logged, so a run cannot evidence #43(b)'s polling premise | DONE | **Landed** (PR #118, `af367c7`) · #43 |
| 47 | OQ-5 — decode the `tips` / `control` / `calib` `*_name` jump tables by the same method the data/process families used | DONE | **Landed** (PR #123) |
| 48 | V4 — the `pose` message + the `follow` camera mode (scanner POV) | DONE | **Landed** (PR #121, `4118b46`) · #40 · #54 · #56 |
| 49 | V3 — `ReplayCaptureSource` + one recorded session, so the viewer develops without the scanner | DONE | **Landed** (PR #122, `13797a7`) · #51 |
| 50 | OQ-1's residual — the literal `this+0x167` payload and the device family's per-call-site `data` values | DONE | **Landed** (PR #129) · #47 |
| 51 | V3's follow-ups — the §6 seam's full unification, the pose record site, the replay's length bound, the log-string tidy | DONE | **Landed** (PR #128, `0bad588`) · #122 · #48 · #105 |
| 52 | OQ-4's residual — the thread that invokes the device-family listener, and whether its callback can re-enter | DONE | **Answered** — the device-family invoke is unsynchronised (no lock/atomics); re-entrancy yes. · tests/results/** · 3ec8432 |
| 53 | V5 — the WebGPU client, with the WebGL2 fallback (the viewer family's last member) | HW | **Landed** (PR #137 @ `7f60c9d`, merged `4b5605d`) |
| 54 | `ui/app.js` fails to parse on main — a duplicate `const frame` binding kills the whole page | DONE | **Fixed and verified** (PR #138 @ `b46662b`, merged) · #41 · #48 · ecb4043 · #137 · #53 · #56 |
| 55 | The config **apply** surface — how a consumer changes a scan's mode/exposure/FOV through the pinned ABI | DONE | **Answered** (`471c599`, this PR) · #29 |
| 56 | A stalled/slow client crashes the whole `scan_server` process via an unsuppressed `SIGPIPE` — not just dropped for that client | DONE | **Fix merged** (PR #146 @ `e8cf51c`, merge `9132adf`) · **Acceptance met** (`tests/results/2026-10-18-stall-client-and-v5-visual.md` + the run's log entry) · tests/results/2026-10-18-scan-server-backpressure-follow-smoke-rerun.md · #41 |
| 57 | The Win64 gate — does Creality's Windows build export the same `obscan_*` surface as the macOS one? | DONE | **Answered — yes** (`d288245`) · docs/recon/findings/win64-surface.md |
| 58 | The config-setter residues — OQ-S13 (the per-gate status-code map) + OQ-S14 (the plane setter's arg order) | DONE | **Answered** (`a14640f` on `recon/next`; `1f729ec` as landed) · #55 · docs/recon/findings/scan-live-data.md |
| 59 | `G1`'s adoption row — turn the corpus's live-plane-removal claim into a spec (the ABI + the decision fn), with a **bind vs reimplement** verdict | DONE | **Answered** — the re-homing question dissolves (same object, two layers); verdict: bind the toggles, reimplement the decision; the fork pin pair (`dac0e7d` / `e6946c5e`). · docs/recon/** · e842e71 · docs/recon/findings/plane-removal.md |
| 60 | `OQ-S12`'s row — the pose's **publication trigger**: declare the export, or document the getter-only path | DONE | **Answered** (`61aceec`) · #48 · #40 |
| 61 | `A1` — serve `ui/` from the capture process, so the client stops needing an external static server | DONE | **Landed** (PR #177 @ `42c2366`) — `scan_server` serves `ui/` on the same port as the WS; README one-process. |
| 62 | `OQ-7b` — the listener-less with-device run (retire or reopen the OQ) | HW | **Runnable via `smoke_test`** — the listener-less tool (it prints `get_device_num() → count=N`); the OQ-S6 half is answerable from existing records, no run. · tests/results/** |
| 63 | `A2` — the `.app` bundle + a WKWebView window (`MACOSX_BUNDLE`, `Info.plist`, icon) | DONE | From rev 11's app workstream: a programmatic ObjC++ shell (no storyboard), `.icns` via `iconutil`, the webview loading the page from the app's own origin (A1's serving). **Operator default**: no vendor dylib in the bundle — require the installed scanner app, resolve its path, fail with a dialog. Wall: builder edits `engine/`, `tools/`, `CMakeLists.txt`; no protocol/header change. **Landed** (PR #180, `3cd2b45`): the `.app` + WKWebView shell — programmatic ObjC++ shell, `MACOSX_BUNDLE`, generated `Info.plist`/`.icns`, ad-hoc signed; the webview loads the server's own origin; no vendor dylib bundled; build verified in the room. `BUILD` → `DONE`. |
| 64 | `G6`'s voxel-size half — align our tool's voxel sizes with the SDK's (the corpus claim, re-derived) | DONE | From rev 11's G-goals (G6, half open): the corpus's `pipeline-quality-assessment.md` §3.1 claim is frozen — re-derive which voxel sizes the SDK/vendor app uses versus our tools, where they are set, and give the alignment verdict (a build row if a change is warranted). Recon owns `docs/recon/**`. **Answered** (`370242d`): **the alignment claim does not survive** — the app's voxel values are per-mode *and per-session* (`scanParamConfig.dat`: laser 0.1/0.05, infrared 0.5/0.25; the corpus's "0.5/0.25" is one captured session, not a reference), and our tools pass **0.1/0.05** — the operator's laser values, at the minimum of the app's ranges. Nothing to change; a coarser grid would be a deliberate bounded knob, not an alignment. Also mapped: `ObScanModeConfig`'s `voxel_size@0x20`/`detail_voxel_size@0x30` (creation-only, else the whole-config reconfig) and the meshlization config's own voxel (getters `0xcb8c`/`0xcc04`, undeclared). `RECON` → `DONE`. |
| 65 | `A3` — lifecycle: spawn and supervise the worker, surface a crash, quit = graceful `pause`/`stop`/`destroy` | BUILD | After `A2` (rev 11). **Dispatched** — `A2` landed (PR #180, `3cd2b45`); A3 builds on its shell. Inherits `OQ-S6`'s answer (the teardown tail ≈ 11 s against the 600 s budget; ≈ 19 s on the 15.6M case) — design against it, no run owed. Wall: the app + `engine/` + `tools/`; no header change. |
| 66 | `A4` — settings and failure paths (missing-SDK dialog, port, session length) | TODO | After `A2` (rev 11). |
| 67 | `OQ-S3` — read both lock sites statically (the `+0x2e0` vs `+0x320` asymmetry) | DONE | From rev 12's OQ sweep (live; trigger: a torn/partial cloud observed, or a second reader added; cheapest retirement: the static read). Deliverable: both sites read, the asymmetry explained or dismissed, and the OQ retired or given a sharper trigger. Recon owns `docs/recon/**`. **Answered** (`12013d3`): `OQ-S3` **retired** — the `+0x2e0`/`+0x320` asymmetry is a **vendor race**: `+0x2e0` is the model-cloud slot's dedicated lock (used coherently by `get_model_cloud`/`_ptr`/`update_model_cloud`/`_with_bbox`), while the C-facing `get_raw_cloud` (`0x702f50`) takes the class-wide `+0x320` and **does not exclude the writers** — none of the 13 `update_model_cloud` callers holds `+0x320` before the call. `RECON` → `DONE`. |
| 45 | No server-side log names the V1 protocol version — a run's log alone cannot show what is on the wire | DONE | **Landed** — the server logs the protocol version at startup, plus teardown counters. |


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

Dated one-liners live in [`log/`](./log/) — **one file per entry**, named `<date>-<slug>.md`.
Add a new file for a new entry (never edit an existing one), so concurrent landings cannot collide
on a shared tail. The entries from 2026-10-16 through 2026-10-18 are the first files there.
