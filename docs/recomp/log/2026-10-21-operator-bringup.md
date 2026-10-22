# 2026-10-21 — the operator bring-up: the probe, the depth, the live ack, the #71 A/B

## The records (PR #470)

- **Landed**: `plan178-daemon-stopped-probe`, `plan182-178-probe-correlation`, the four
  blocked #184 live-IR attempts (`rerun`, `rerun2`, `attempt3-blocked`, `attempt4`), and
  `plan71-attempt10-ab`, `plan71-attempt11-ab`.
- **Held for the tester**: `plan175-depth-telemetry-final` and
  `plan184-prop192-ir-attempt5` (both raws **abridged**, no sha256);
  `plan71-attempt11-portcheck` and `plan71-attempt12-ab` (clock/Sequence defects).

## The findings

- The **daemon-stopped probe adds no new data** (the 55 lines match); `--table` still says
  `unknown` and `descriptor_table.h` is unedited. The fields are printed — **#186** fills
  the table (build1).
- The **correlation** resolves IR=3/depth=1/RGB=5/HID=IMU=6 by interface name; the 10-second
  watch saw all `alt=0` (no stream), so the live-stream acceptance stays open.
- The **depth is empty even on a fresh daemon, depth-first** (`data=0 empty=62`,
  `first_frame=none`); the restart/depth-first remedy is **falsified**; every SET is
  `vendor no ack (0 B)`. The lead is the daemon depth pipeline / SET-ack gating — **#187**
  (recon1).
- The **live SET ack is 0 B on both ports**; the #132 capture's depth 64-byte `str` is the
  only ack-bearing form seen. The #464 fix is **untested live, not falsified** — **#187**.
- The **#71 A/B is one-sided**: both toggles accepted, the step appears only at the
  **disable** boundary (1660.7 → 9967.7 → 17304.5 pts/tick); the enable boundary does not
  drop; `our_plane_removal fits=0 kept=0 cut=0` (the vendor path only). `HW` stays.

## The tester's repairs

The two abridged raws (re-emit the full stdout or declare the abridgement), the
`attempt11-portcheck` clock line, the `attempt12` wall-clock Sequence, and the records'
HEAD/sha256 backfill (the P12-lite).

## The follow-ups

- **#186** (build1) — fill `descriptor_table.h` from the probe + the mapping.
- **#187** (recon1) — the live ack's identity and the depth pipeline.

## Landed (20:27)

- **#472 (#186)** — the descriptor table filled from the probe and the #182 names (IR=3/0x82/Y8
  960x600/960; depth=1/0x81/Y8 480x300/840; RGB=5/0x83/YUY2 1280x800/2560; HID=6/0x85/248/25).
  The control report id/field map stays unknown; the report-descriptor read is the last field.
- **#473** — the four held records landed with their declared abridgements and the aligned
  sha256 citations (the tester's values predated the raw headers; the correction is mechanical).

## The CI flake, named (#188)

The #466 checker showed the recurring `ctest exit 8` is **one intermittent test**:
`helper_xpc_test (SEGFAULT)` — the XPC test from the helper service, crashing on the headless
runner. The deflake is with build2.

## Session swap

recon1: `ses_ea764fc29ffeH68CjSl6fUFMKU` -> `ses_e9f05ac28ffeFYYElodMwgCDq1`. The prior
session hit its tool-step limit in a loop at 92M context; the fresh one delivered #474 (the
#187 finding).

## The takeover's remaining three (T8)

- **#187 (the depth)** — the T8 card (fresh daemon, depth first, the ack bytes); the finding
  (#474) is in verify3.
- **#189 (the HID report read)** — build1 in flight; the probe run follows.
- **#71** — the T8 two-sided/geometric card.

## The last code landed (21:16)

- **#474 (#187)** — the live-ack/depth finding (the capture's 64-B `'RB'` vs the live 0-B;
  the corrected RPCServer next test; the provenance).
- **#477 (#189)** — the HID report-descriptor read in the probe: the 25 bytes, the parse, and
  the explicit output-report-vs-input-only conclusion.
- **#478 (#188)** — the `helper_xpc_test` SEGFAULT: a helper use-after-free (a raw service
  pointer in the XPC handler; the async cancel after teardown), fixed with a `HandlerState`
  shared_ptr and a deterministic drain; 0/1000 vs 10/500; `ctest` 32/32.

## The open design gates

The e10 source; the frame profile/resolution; the control transport (pending the operator's
HID probe); the depth stream/format; the privilege tier (the no-sudo test); the switch/A-B/#126.

## The operator runs (T8)

The UVC live capture; the HID probe (daemon stopped); the fresh-RPCServer depth/ack test; the
#71 two-sided/geometric observable; the no-sudo claim; the vendor A/B.

## The #71 prerequisite (operator, 2026-10-21)

The flat-plane removal test is **not ready**. The vendor's plane identification needs the
scene **orientation** and the scanner's **motion across the plane**; a stationary toggle
cannot identify the plane, and the a12's one-sided count is consistent with exactly that. The
T8 card C is withdrawn; a redesigned oriented, moving scan is the prerequisite. The rest of
the T8 round (the UVC capture, the no-sudo claim, the HID probe, the vendor depth/ack, the
browser/HUD) is unaffected.
