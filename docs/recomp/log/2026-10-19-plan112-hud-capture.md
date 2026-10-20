- 2026-10-19 — **#112 HUD capture landed** (`tests/results/2026-10-19-plan112-conditions-4-6-hud-capture.md`): conditions 4–6 re-run with
  the HUD measured three ways (server, coordinator samples, operator recording) — `data` 5.8–10 fps, `render` 59.5–60.1 fps; condition 6
  was a genuine UI click, and the deeper pass found **two** pause/resume cycles. The visual analysis is the hw session's own 1 fps/320 px
  read, attributed and not re-derived. Two follow-ups: **#137** (the exposure slider drifting 900→1) and **#138** (IMU: the SDK opens the
  port and the capture holds 8,643 × 248-byte records at ~100 Hz, while nothing in our pipeline reads it).
