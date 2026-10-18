- 2026-10-18 — recon: **the Win64 gate is answered — yes** (row #57, `d288245`).
  The Windows payload ships `lib_orbbec_scan.dll` (x86-64, 3.5.3.**260921** — the same
  build as the pinned macOS dylib) with **0 missing** exports against our 215 and
  **36 Windows-only** (debug family, context batch/device/gpu calls, k3/temperature/ahb/i2c,
  session cache/config). The workstream is a port; the planner's seeding steps unblock.
  Findings: `docs/recon/findings/win64-surface.md`; no header change.
