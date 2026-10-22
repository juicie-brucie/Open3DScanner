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
