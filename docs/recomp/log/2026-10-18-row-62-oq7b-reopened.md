- 2026-10-18 — hardware (`hw-test`, `wt-hw`, branch `hw/next`): #62's run —
  `smoke_test` (listener-free), `get_device_num() -> count=0`. Reopened, not
  retired: the vendor's own log shows the watcher completing detection
  ~1s *after* this single poll, with zero listener calls — corroborates
  PLAN #7 (the watcher runs and detects with no listener), but the
  count=0 result is a race against async latency, not evidence either
  way for the narrower claim ("does the getter return nonzero with no
  listener, given time") — that needs a delayed/repeated probe. Also hit
  the documented `obscan_context_destroy()` SIGSEGV (README "Known
  issues") — `smoke_test` has no interposer, so it's unprotected; worker
  isolation caught it cleanly, exit 2, no hang. Flagged: this tool's
  exit 2 ("recovered from known crash") collides in meaning with hw-test
  rule 10's "skipped, no dylib" exit-2 convention.
