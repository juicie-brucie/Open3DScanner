> **Handoff to dispatch (opencode), 2026-10-24.** Consolidated state of this
> session's work so dispatch can land it, apply board moves, and pick the next
> WO. Nothing in this session went through a PR; nothing touched `PLAN.md` or
> `PR-QUEUE.md`. Two companion docs carry the detail: the UVC/Phase-D dossier
> (`2026-10-24-uvc-daemon-phase-d-dossier.md`) and the daemon-replacement
> scorecard (`2026-10-24-daemon-replacement-scorecard.md`). All three of these
> `docs/recomp/log/*.md` files are **uncommitted drafts** mirrored into both
> worktrees.

# Session handoff — daemon-replacement + UI work

## 1. State of play, by branch

Two worktrees / branches, deliberately kept separate (operator decision):

- **`hw/next`** (worktree `Open3DScanner-wt-hw`) — the raw_capture / daemon-
  replacement chain. **All committed.** 18 commits this session
  (`95218cc`…`a1561c1`): the UVC capture chain (alt-setting, clearStall,
  negotiation, pipe-refresh, the four interface-lookup attempts, the classic
  IOUSBLib path `de94947`, `--diag-interfaces`), the frame/format-index probe
  fixes, and every 2026-10-24 hw-test record.
- **`xcode/user-test`** (worktree `Open3DScanner-wt-xcode`) — the product/UI
  axis. `hw/next` has been merged *into* it repeatedly (never the reverse), so
  it has everything above **plus** uncommitted UI/app/scan_server work (§2).

Build/test status (xcode/user-test, 2026-10-24): **OpenScanner app builds
clean; `project_save_test` 22/22; `ws_broadcast_counters_test` 36/36; all
modified UI JS passes `node --check`.** Known-good.

## 2. Uncommitted work on `xcode/user-test` (needs landing)

Three groups, all uncommitted. **Note:** `tools/macos_app/main.mm` is touched
by *both* the feature work and the white-screen fix — they are entangled in
that one file and will likely land together (or need hunk-level splitting).

**(a) Product/UI feature work** (operator-authored):
- `tools/scan_server/main.cpp` — shared cloud/project state, status fields,
  save/upload/reconnect commands, unbounded live loop
- `tools/scan_server/CMakeLists.txt` — registers the new test
- `tools/macos_app/main.mm` — removed session-limit UI, Reconnect menu, neutral titles
- `ui/index.html` — Reset view, Axes toggle, Project panel
- `ui/app.js` — left-drag orbit, axis triad, project/network/reconnect handlers
- `ui/render/webgl2_renderer.js`, `ui/render/webgpu_renderer.js` — line pipeline + axis triad
- **New:** `tools/scan_server/project_save.h`, `tools/scan_server/project_save_test.cpp` (22/22)

**(b) White-screen debug fix** (operator-authored):
- `engine/src/ws_broadcast_server.cpp` — **the actual fix**: a 750 ms recv
  timeout on the handshake so an idle speculative WebKit connection can't
  starve the single-threaded accept loop; timeout cleared after the WS upgrade
  so the data stream is unaffected. (Engine code — agent tier.)
- `tools/macos_app/Info.plist.in` — `NSAppTransportSecurity ›
  NSAllowsLocalNetworking` + `NSLocalNetworkUsageDescription` (ATS/local-network
  gating for the loopback load)
- `tools/macos_app/main.mm` — dark window/WebView bg + "Starting OpenScanner…"
  splash (no white flash), `loadPageWhenServing:`, `OPENSCANNER_SYNTHETIC=1`
  device-free app mode, extracted `installWorkerTerminationHandler`, nav
  diagnostics (`didFinishNavigation`/`didFailProvisionalNavigation`)

**(c) Session docs** (Claude-authored working drafts): this handoff, the
dossier, the scorecard — all under `docs/recomp/log/`.

## 3. Recommended PR split (extends the dossier §6)

Tiers per `scripts/verify-pr.py`: `engine/** tools/** ui/** CMakeLists
tests/results/**` = **agent tier** (verifier needed); `docs/recomp/log/*.md` =
**script tier**.

From `hw/next` (already committed):
- **PR A** — `tools/raw_capture/**` + its device-free tests (210/210). Agent tier.
- **PR B** — the `tests/results/2026-10-{21,24}-plan-uvc-*` + `plan-b/c/f/170`
  records. Agent tier (record-provenance, #21). Exclude any oversized `.rec`.
- **PR (daemon chain code)** — the `iokit_source.mm` chain lands with PR A.

From `xcode/user-test` (uncommitted — commit first):
- **PR E** — the UI/app/scan_server feature work **+ the white-screen fix**
  together (§2a+§2b; entangled in `main.mm`). Agent tier; visual check wanted
  (left-drag orbit, axis triad, Project panel, splash, save/upload/reconnect).
  Includes `project_save.*` and the engine `ws_broadcast_server` fix.
- **PR F** — the three `docs/recomp/log/*.md` session docs. Script tier,
  provenance only.

Earlier UI commit `3cf3f7f` (density gradient, camera controls, Xcode signing)
is already committed on both branches; fold into the UI story or land per
dossier §6 PR C. **`build-xcode/` + scratch files** (`grep_list`,
`sync-to-main.sh`, `ui/watch.sh`) are hygiene (dossier §6 PR D) — confirm
ownership before touching; not Claude's.

## 4. Board moves to apply (dispatch owns `PLAN.md`)

- **#171 / #178** stay `HW` — raw_capture live capture still blocked; classic
  IOUSBLib path (`de94947`) built but not yet run on hardware.
- **New rows / finding to file** (from the dossier §4): (i) **HID is
  input-only** — laser/exposure must come from the vendor RPC property channel
  or UVC extension units, not a HID output report (corrects the original row
  premise); (ii) **the RPC IR frame is 4× the UVC IR frame** — light-state
  aggregation hypothesis (`ob_set_ir_frame_light_type` 0–3). Phase-D design
  inputs, not merge details.
- **UI baseline** (`plan-uvc-ui-baseline.md`, PASS) — vendor pipeline intact,
  best run yet; file as the baseline record. Row for the white-screen fix and
  the new UI features once PR E is scoped.
- The 2026-10-24 hw-test records each state their own proposed row move in
  their Board section; apply from there.

## 5. Open gates / blockers

- **Keystone:** the first hardware run of the classic IOUSBLib capture path
  (`de94947`). Dispatch note is drafted (dossier/session). If it captures, the
  shim's live source + the vendor↔shim A/B unblock. `ioreg` evidence (RPCServer
  seizes the interfaces from UVCAssistant live, dossier §2b) makes it a
  well-grounded bet.
- Calibration residuals (#95): plane-struct field map (`OQ-S15a/b`) + checksum
  validation — gate a correct Phase C.
- `scan_server` does not yet read `daemon_switch` — no replacement is wired
  into the product.

## 6. Scorecard, one line

Phase A (spec) ✅, L2 bypass via `raptor_client`/C1 ✅ (SDK-free live frame
pull), L3 full removal (C2/`raw_capture`) blocked, **L4 reconstruction ~0%**,
nothing integrated into the product. See the scorecard doc for the full map.

## 7. Candidate next WOs (discrete, Claude-completable, unblocked)

Drafted this session; detail on request. In recommended order:
1. **WO-1 — Phase C seed: laser-line extraction** on recorded IR frames
   (device-free, foundational, fully unblocked). *Recommended next.*
2. **WO-2 — Own point cloud via triangulation, A/B vs the vendor cloud**
   (highest value toward L4; vendor A/B gated on a paired recording).
3. **WO-3 — Offline daemon drop-in proof**: `raptor_shim` serves a full
   recorded session to the vendor's *own* SDK client (advances L3/C2 without
   the live-capture blocker).
4. **WO-4 — Close the #95 calibration residuals** (recon; de-risks WO-2).

## 8. Process notes for dispatch

- Nothing PR'd, nothing merged to `main`, `PLAN.md`/`PR-QUEUE.md` untouched
  this session — all board moves and PRs are dispatch's to apply.
- One record was overwritten in place mid-session (`plan-uvc-probe-indexfix`,
  BLOCKED→PASS) before the write-once discipline was reinforced; it is now
  committed in its corrected form. Flagged for the record.
- The hw-test runs used a general-purpose agent carrying the `hw-test` role
  verbatim (the registered `hw-test` subagent type wasn't available in the
  session); records follow the hw-test template and discipline.
- The two UI branches (`hw/next` served UI vs `xcode/user-test` app UI with the
  gradient) are an **intentional** split; do not merge unless the operator asks.
