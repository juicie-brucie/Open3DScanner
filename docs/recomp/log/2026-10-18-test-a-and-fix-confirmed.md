- 2026-10-18 — hardware (`hw-test` + main session, `wt-hw`, branch `hw/next`):
  "Test A" — a 5-minute `scan_server --live` run for #41/#48. Hit #54's
  page-breaking `ui/app.js` `SyntaxError` on the client side (filed
  independently by the dispatcher the same day, `BUILD` → now `DONE`, PR
  #138) — so only the server-side halves of #41/#48 could run. Those still
  landed real evidence: **#43/#46 fully closed** — per-poll destroy logging
  fired for real (`obscan_session_destroy` polled 168 times, 167 refused
  with `MULTI_CALLED` over 17.2s, then succeeded), so all five of #43's
  teardown observations are now directly evidenced, not inferred; #43/#46
  → `DONE`. #32 re-confirmed (point count sustained past 1.36M live).
  #41/#48 stay `DONE` (landed) but their client-side hardware confirmation
  is still outstanding — #54's fix (PR #138, `b46662b`) landed after this
  run, so a straight rerun of Test A is what's needed next, not a new test.
