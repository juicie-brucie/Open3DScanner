- 2026-10-18 — dispatcher: #37's board note corrected — the teardown tail did not fit the
  420 s watchdog budget (measured from `fork()`), so the serialization was cut off mid-tail,
  not stalled after a completed save; the finding's own correction rides PR #106.
