# 2026-10-24 — the handoff landing: the daemon chain, records, UI, and the board

## The PRs

- **#489** — the `hw/next` `tools/raw_capture` chain onto current `main`: the UVC
  Probe/Commit negotiation, pipe refresh, clear-stall, the interface-lookup iterations, the
  probe format/frame-index pairing fix, and the **classic IOUSBLib path** (`de94947`,
  `CreateInterfaceIterator` + `USBInterfaceOpenSeize`) now the default for
  `MakeLiveUvcSource`. `OPENSCANNER_UVC_IOUSBHOST=1` forces the old path. **Reconciliation
  note:** `hw/next` branched before #487 merged and implemented its own alt-setting fix;
  the merge keeps **#487's** enumeration/selection and folds in hw/next's additional tests
  and clear-stall work. 234 device-free checks pass. The classic path is **not yet run on
  hardware**.
- **#490** — the records: the `hw/next` chain plus the 2026-10-21/23 records (the no-sudo
  claim, the vendor depth/ack, the shim live refusal, the UVC sweep, the full-UI vendor
  baseline, the browser render, the markerless HUD, the #71 orient/toggle pair and its
  three attributed screenshots). 84 files, 25 run records with evidence.
- **#491** — the operator-authored UI/app/scan_server work + the white-screen fix
  (`engine/src/ws_broadcast_server.cpp`: a 750 ms handshake recv timeout so an idle
  speculative WebKit connection cannot starve the accept loop). `3cf3f7f`'s UI baseline is
  folded in; its Xcode signing block is omitted (#481 already landed signing).
- **#492** — the three 2026-10-24 session docs (handoff, Phase-D dossier, scorecard),
  script tier.

## Board moves

- **#194 `RECON` -> `DONE`** — **#485 landed**: the RPC frame is 1920x1200x1 Y8, 1920-byte
  stride; the UVC IR is 960x600 = 576,000 (stride 960), depth 480x300 stride 840 = 252,000;
  the live `e4`/`e5` are 576000/252000 and the shim's live `start_stream` must advertise the
  **UVC** dimensions.
- **#151 `HW` -> `DONE`** — the browser render PASS (`clients=1`, webgpu, render 59.9 fps,
  frame age p50 6.8 / p95 15.0 ms, `cloud_frames=3001`, coverage 19%, frame #1655). The
  record does not state `FRAME_READY/s`; the raw computes ~9.4/s. The operator's read is
  attributed.
- **#93 stays `HW`** — the HUD rendered `FAILURE_MARKERLESS (5)` at #3571 and `SUCCESS (0)`
  at #4199 with no `k/3` suffix, but the owed `LOST, LOST, MARKERLESS` transition was not
  captured.
- **#71 stays `HW`** — the oriented/moving A/B is a valid **negative**: all three toggles
  accepted, the operator reports "nothing visually changed whatsoever during those toggles"
  (attributed); the counts are confounded by accumulation; the scene-clearing was never
  confirmed.
- **#171/#178 stay `HW`** — the 2026-10-24 sweep: `--probe` PASS throughout, every capture
  0 frames; the failure moved from `Unable to send IO` to `GET_CUR` to `interface N is not
  on the device`. The old "endpoints unknown" framing is superseded by the filled table; the
  blocker is the IOKit interface lookup/open, which the classic path targets.
- **#181 stays `HW`** — the user-scope claim PASS (registered in `gui/501`, `claimed`,
  `claimed-by-helper`); the unplug/`--once`/`--seize` pair is still owed. **#177 stays
  `RECON`** — the falsifier did not fire; the remaining gap is the stable self-signed
  identity (the installer re-signs ad-hoc).
- **#187 `DONE` with a correction** — the 2026-10-21 live depth ack is **64 B** on a fresh
  daemon (IR 0 B), contradicting "0 B on both ports"; depth frames: one 12,276-B band then
  empty. **#175 `HW`** gets the depth `data=1`/`first_frame=274us` note (vs the 18:35
  `data=0`/62); **#174 `DONE`** gets the depth-band caveat.
- **#199 `TODO`** gains the UI baseline PASS note (the vendor pipeline ran end to end;
  `hw/next`'s pre-gradient UI is the intentional branch split).
- **#200 filed `BUILD`** — Phase C seed, sub-pixel laser-line extraction on recorded IR
  frames; dispatched to build2 (`build2/200-laser-line`).
- **#201 filed `DONE`** — the control path is not HID (input-only, 25-byte descriptor,
  62x32-bit input = 248 B, no output); control stays the vendor RPC properties or the UVC
  extension units.
- **#202 filed `RECON`** — the IR 4x light-state aggregation hypothesis; needs a UVC IR frame
  and an RPC IR reference under their mutually exclusive preconditions.

## Next

- **The keystone hardware run**: the classic IOUSBLib capture path (#489) — depth first,
  vendor stopped and power-cycled, `--probe` + `--diag-interfaces` first. The order is with
  the operator; the outcome (full capture / seize refusal / iterator failure) decides whether
  the shim's live source becomes reachable.
- **#200** (build2) is the device-free Phase C seed; **#198** (the Claude UVC/daemon
  workstream) stays device-blocked; **#199** (the Claude UI/Xcode workstream) remains the
  operator's to start.
- The two UI branches stay separate; the `xcode/user-test` PR (#491) is the app UI, not the
  `hw/next`-served UI.
