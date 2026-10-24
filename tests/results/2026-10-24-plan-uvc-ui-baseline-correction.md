# Correction — the UI-baseline "density colouring" label

- Date: 2026-10-24
- Corrects: `2026-10-24-plan-uvc-ui-baseline.md`, check 10 and the row-move note.
- Correction: the red under-covered / white well-covered dots in this run are the **old
  coverage overlay** served from `hw/next`'s **pre-gradient** UI, **not** the density
  gradient. The density gradient lives on `xcode/user-test` and was not present in this UI.
  The dossier §4a records the same: the run "served `hw/next`'s **old** red/white overlay,
  not the density gradient".
- The observation itself (red/white dots visible) stands; only the **"density colouring"**
  label is wrong.
- Effect: none on the vendor-pipeline PASS; this record is **not** evidence about the
  density-gradient work (#191).
- Source: verify1's #490 verification (2026-10-24); dossier §4a.
