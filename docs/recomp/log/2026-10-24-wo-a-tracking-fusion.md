# 2026-10-24 — WO-A: frame-to-frame tracking + multi-frame fusion (dispatched)

- **Scope:** a track + fuse stage — per-frame pose (marker-based; the marker/pose fields
  decoded in #44; the vendor's marker-detect + bundle-adjust approach, with **Ceres** for the
  bundle adjustment as the feasibility doc names) — then fuse the per-frame triangulated clouds
  into one consistent accumulated cloud. Device-free tests on recorded multi-frame sessions
  (drift/consistency metrics); the accumulated-cloud A/B via #207's `cloud_compare`, gated on the
  paired vendor recording (#207 `HW`).
- **Why now:** it is the current gate and the hardest remaining L4 piece. Without it, the landed
  laser-line (#200/#205) + triangulation (#207/#509) produce isolated stripes, not a scan. Purely
  algorithmic; no hardware.
- **Depends:** #207/#509 (triangulation/compare), #206 (the emitter-line association), #208 (the
  calibration adapter: primary 29-plane set + real intrinsics), #44 (DONE), and the #132
  multi-frame sessions.
- **Risk:** medium-high — tracking is the real algorithm work.
- **Dispatched:** to the Claude xcode main agent (operator, 2026-10-24); board row #209. Ceres is
  a third-party dependency; confirm the build/third-party policy before adding it. The vendor A/B
  is the #207 HW card.
