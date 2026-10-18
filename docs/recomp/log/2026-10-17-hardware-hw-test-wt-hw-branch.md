- 2026-10-17 — hardware (`hw-test`, `wt-hw`, branch `hw/next`): #25's OQ-S5
  acceptance run — `scan_server --live` for a 30s session, exit 0, clean
  pause→mesh→stop→destroy teardown. `color_cloud num_points` goes nonzero
  (292→883, growing) from tick 50/300 onward with `paused=0` throughout —
  live points publish during active scanning. #25 → `DONE`. Unexpected:
  `raw_cloud num_points` tracked `color_cloud` in lockstep this run,
  contradicting #23's "always 0 during scanning" — filed as **#30**.
