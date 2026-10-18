- 2026-10-18 — recon: row #68 answered — the four residues settled: the `+0x3c` guard is
  statically unreachable (constructor zero-stores are its only writers); the Laser gate tests
  `projection == 1`, not the `+0xe1` latch; `OBScanPlane_` was already declared (#59); `OQ-7d`
  closed. #68 → `DONE`.
- 2026-10-18 — board: the filing line (above, in `row-68.md`) was restored — the landing had
  clobbered it, and the convention is event-per-file (`row-58.md` + `row-58-answered.md`).
  Row #68's filed premise ("`OBScanPlane_`'s layout is the one that unblocks the header
  declaration") is marked stale: #59 declared it at `dff01c7`. From the verifier's NEEDS CHANGE
  sweep on PR #189.
