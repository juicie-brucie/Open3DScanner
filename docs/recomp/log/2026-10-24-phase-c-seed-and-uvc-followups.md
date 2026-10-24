# 2026-10-24 — the Phase C seed lands; the UVC blocker narrows to a lookup

## Landed

- **#495** — the Phase C seed (row #200): `engine/src/recon/laser_line.{h,cpp}` + its test.
  Synthetic: a **measured** sub-pixel bound (headline mean 0.0192 px; worst sweep mean 0.0635,
  max 0.242 px). Recorded #132: 180 frames, 99.8% rows-with-line, ~3.66 centres per
  row-with-line, the persistence/gap metrics, a PGM dump. The honest limitation: the recorded
  IR scene is **multi-line**, so the extractor returns candidates; track selection is #206.
  `ctest -LE hardware` 35/35. The comment-only punch-list (the MAD note; the full-frame bin
  note) landed at `d983078`.
- **#494** — the Phase-D design-inputs finding: the HID is **input-only** (#201 settled); the
  IR 4x light-state hypothesis is **not supported by the evidence** — the serializer is a
  pass-through and the modes differ, but producer/PAL-level aggregation is not excluded; the
  paired UVC-vs-RPC frames are the decider (#202 stays `RECON`).
- **#496** — the classic IOUSBLib run (FAIL): probe/diag PASS, interfaces visible/unowned;
  the capture fails at `IOCreatePlugInInterfaceForService` with `0xe00002be`
  (**`kIOReturnNoResources`**, not `kIOReturnNotPrivileged` = `0xe00002c1`). The classic device
  user client is the wrong plug-in for the `IOUSBHostDevice` service.
- **#498** — the modern IOUSBHost run on main (FAIL): probe/diag PASS, UVCAssistant owns
  interfaces 0-5; the capture fails at **`FindInterfaceService`** (`interface 1 is not on the
  device`, 15 retries), **not** at the seize. The filtered registry match is the blocker; the
  fix goes to the #198 Claude workstream (use the in-process enumeration the diag proves).
- **#497** — the board + the UI-baseline correction addendum (the "density colouring" label was
  the old red/white overlay from `hw/next`'s pre-gradient UI).

## Board

- **#200 `BUILD` -> `DONE`** — #495; the fast-follow #205 (RCRP reader dedup) and the next
  stage #206 (track selection/association).
- **#202 stays `RECON`** — not supported / not refuted; the empirical pair decides.
- **#204 stays `RECON`** — the xcode-recon WO-4 draft (CRC-32 8/8, the plane struct) is under
  recon2 adoption.
- **#171/#178 stay `HW`** — the classic path is a dead end; the modern path's blocker is the
  interface lookup, handed to #198.

## Corrections of record

- The Claude review's "`docs/recon/findings/frame-geometry.md` does not exist" was a **stale
  worktree** (`xcode/user-test` predates #485). The file is on main and in #495's base; §2 is
  "The RPC Frame Geometry — Confirmed". The reference was left unchanged.
- The classic record's `kIOReturnNotPrivileged` label and the modern record's "seize" lead were
  both wrong; correction addenda name the exact error codes/branches (#496, #498).

## Next

- **#198** (Claude): fix the modern interface lookup and re-run the deterministic capture.
- **#205** (build2): move the recorded-frame CLI to `tools/laser_line/`, use the shared
  `Reader`, keep the ctest synthetic-only.
- **#206**: track selection/association — the real open work #495 leaves.
