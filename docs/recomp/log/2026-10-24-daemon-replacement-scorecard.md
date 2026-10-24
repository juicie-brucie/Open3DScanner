> **Status assessment — point-in-time, 2026-10-24.** Where the daemon-
> replacement effort (Phase D / C2 and the layers around it) actually stands
> against the goal of running the scanner with no Creality or Orbbec code on
> the host. Companion to `2026-10-24-uvc-daemon-phase-d-dossier.md` (which
> covers the L2 live-capture work in depth). This is a synthesis for the
> dispatch handoff, not a record to cite verbatim in `PLAN.md`; pull the row
> numbers and commit SHAs it points at instead.
>
> Evidence basis: the current `hw/next` tree (code read directly), the four-
> layer model in `docs/recomp/FULL-INDEPENDENCE.md`, the Phase D rows in
> `PLAN.md` (#170/#171/#172/#177/#178/#179/#180/#181/#187/#189/#191), the
> 2026-10-24 test records, and the UVC dossier.

# Daemon-replacement scorecard — 2026-10-24

## The target (from FULL-INDEPENDENCE.md)

Four proprietary layers sit between the app and the scanner, bottom to top:

- **L1 — Firmware** on the scanner (sensors, laser driver, flash, USB/UVC).
- **L2 — Orbbec device library** (`libOrbbecSDK 1.7.1`) — UVC/HID device I/O.
- **L3 — Creality `RPCServer`** — the root daemon that owns the USB device and
  serves frames as msgpack-RPC on localhost (one TCP port per interface).
- **L4 — OBScan** (`liborbbec_scan`) — the closed scanning algorithms: laser-
  line extraction, triangulation, depth, marker tracking, fusion, meshing.
  **This is the bulk of the vendor's value and the bulk of what the product
  currently binds.**

Planned replacement order (FULL-INDEPENDENCE §6): Phase A (spec the protocol)
→ Phase B/C1 (our own RPC client to the daemon) → Phase C (own the scanning
math, L4) → Phase D/C2 (replace the daemon, L2+L3). True independence needs
**both C and D** — D alone removes the daemon but L4 reconstruction still
needs OBScan.

## Scorecard by layer

| Layer | Our replacement | Status | Detail |
|---|---|---|---|
| **L4 OBScan** (scanning algorithms) | *(none)* | **~0% — not started** | No triangulation / laser-line / depth / fusion code exists in `engine/` or `mesh/`. `tools/scan_server/main.cpp` calls `obscan_context_create` / `obscan_scan_preview` / etc. directly. `mesh/src/mesh_tools.cpp` is post-processing only (libigl decimate + manifold repair). `engine/src/obscan_mesh_bridge.cpp` only *converts* the vendor's `ObScanTriangleMesh` output into our buffers — it does not mesh. |
| **L3 Creality daemon** | `raptor_client` (C1), `raptor_shim` (C2), `macos_helper` (privileged claim) | **Partial / blocked at live** | Protocol fully mapped and implemented. `raptor_shim` serves an **unmodified** `raptor_client` from recorded #132 bundles (T7 offline A/B, PASS, PR #457). `macos_helper`'s **no-sudo user-LaunchAgent device claim works end to end** (`plan-b-nosudo-claim.md`, PASS). But the shim's **live** source (`LiveSource` → `raw_capture::FrameSource`) is blocked on L2. The vendor↔shim same-scene A/B has never run live. |
| **L2 Orbbec device lib** — two separate replacement routes | **C1:** `raptor_client` (SDK-free RPC client). **C2:** `raw_capture` (UVC/HID over IOKit) | **C1 PASS; C2 blocked** | **C1 — libOrbbecSDK bypassed, CONFIRMED.** `raptor_client` ("No OBScan, no libOrbbecSDK, no obscan_* calls") pulls **live** IR/depth frames straight from the daemon: `plan-c-vendor-depth-ack.md` (PASS) — port 10013 IR `get_frame=10 data=7` (70% share), 10010 depth `data=1`, SET acks accepted. So the Orbbec device library can be removed from the I/O path today — **this is the "passed L2" milestone** — with two caveats: C1 still needs the daemon (L3) running, and it's a frame-pull demonstration, not yet the full Phase-B "drive laser+exposure loops / continuous scan." **C2 — the deeper route (remove L2 *and* the daemon via our own USB) is blocked:** the entire UVC saga (see the dossier), can't claim the camera interfaces (`UVCAssistant` owns them), classic IOUSBLib path built (`de94947`) but not yet run. Protocol fully understood either way (UVC Probe/Commit, 11-element array, banding, indices). |
| **L1 Firmware** | *(out of scope)* | **Research track only** | Explicitly off the critical path (FULL-INDEPENDENCE §7); buys little, high bricking risk. Extracted image is read-only recon input. |

## The three facts that matter most

1. **The shipping product is still 100% vendor.** `scan_server` and
   `tools/macos_app` reference *none* of the replacement tools — the only
   cross-reference is `engine/include/openscanner/daemon_switch.h`, a header
   whose own comment says "the product does not read this yet." Every
   replacement tool (`raptor_shim`, `raptor_client`, `raw_capture`,
   `macos_helper`) is standalone and experimental. The 2026-10-24 UI baseline
   run (~10 fps, the best yet) was OBScan + the vendor daemon end to end.

2. **L2 (libOrbbecSDK) is already bypassable; the *daemon's own USB* is the
   blocked part.** Two different "replace L2" routes must not be conflated:
   `raptor_client` (C1) already pulls live frames from the daemon with **no
   Orbbec library at all** (`plan-c-vendor-depth-ack.md`, PASS) — that is the
   real "passed L2" result. What's blocked is the *deeper* step (`raw_capture`,
   C2): having our **own** code claim the USB device so we can also drop the
   daemon (L3). The shim can replace the daemon's protocol but needs C2's live
   capture to actually *be* the daemon; until then it only replays recorded
   frames. (Encouraging: `ioreg` confirmed the vendor's own `RPCServer` seizes
   these interfaces from `UVCAssistant` live — dossier §2b — so the mechanism
   the classic path targets is proven on this device; just not yet run in our
   code.)

3. **The reconstruction math (L4) is untouched, and it is the bulk of the
   value.** Even with the daemon fully replaced, we would have *raw IR frames*
   and no way to produce a mesh. Phase C must precede true independence and is
   at zero. Rated "HIGH feasibility, medium effort" in the feasibility doc —
   feasible, but a substantial body of work not yet begun.

## What is solidly done (the real progress)

- **Phase A — protocol fully spec'd** (#96 / G-A): the RPC method set, the
  vendor-command framing, the property map, the flash-read transport —
  observation-derived, clean-room.
- **Frame codec / 11-element array / banding** (#124/#131/#174): REA'd, the
  array fully named, the band rule falsified (whole 2.3 MB frames, not bands).
- **Calibration decode** (#95, mostly DONE): 16-byte flash-record header
  decoded, nine record types, temp-comp threshold corrected to 0.2 °C;
  residuals — the checksum fails validation on reassembled records (a lead),
  and the plane struct's field mapping (`OQ-S15a/b`) is run-dependent.
- **L2 bypass (C1 / Phase B) — libOrbbecSDK eliminated from the I/O path:**
  `raptor_client`, SDK-free, pulls live IR/depth frames from the daemon
  (`plan-c-vendor-depth-ack.md`, PASS). Caveats: still needs the daemon (L3);
  the full continuous-scan + laser/exposure-loop half isn't demonstrated
  end-to-end; not wired into the product.
- **"The device is dumb"** confirmed: laser sequencing, auto-exposure and
  depth are all host-side, so the scanning smarts are reimplementable.
- **No-sudo privileged helper claim** (#177/#181): works end to end.
- **The product switch** (`daemon_switch.h`, #179): present, default `vendor`,
  not yet read by the product.

## Bottom line

Two goalposts, neither close:

- **Daemon-free** (no Creality `RPCServer`): the plumbing is ~60–70% built —
  protocol, shim, helper, capture scaffolding — but stuck at the live-capture
  step, and nothing is integrated into the product.
- **Fully vendor-free** (daemon-free *and* no OBScan): further still, because
  L4 (the scanning algorithms) is at zero.

Characterized bluntly: a well-engineered **chassis for replacement** exists
and each piece is proven in isolation, but the **engine** (L4 reconstruction)
does not exist yet and the **fuel line** (L2 live capture) is blocked. The
highest-leverage next unblock is the classic IOUSBLib capture run (`de94947`,
dispatch note ready) — it is what turns the shim from a replayer into an
actual daemon, and the `ioreg` evidence makes it a well-grounded bet.

## Highest-leverage next steps, in order

1. **Run the classic IOUSBLib capture path** (`de94947`) — the keystone L2
   unblock. If it captures, the shim's live source and the vendor↔shim A/B
   immediately become reachable.
2. **If L2 works: wire the shim's live source + the helper claim together**
   and run the vendor↔shim same-scene A/B (#172/#179) — the first proof the
   daemon can be replaced for real.
3. **Begin Phase C (L4) incrementally** on raw frames, cheapest stage first
   (laser-line extraction → triangulation), A/B-tested against the vendor's
   own output while the vendor path still exists as reference. This is the
   long pole for full independence and can start in parallel with 1–2 since it
   only needs recorded frames, not live capture.
4. **Integrate, eventually:** have `scan_server` actually read `daemon_switch`
   and route through the shim — nothing does yet.
