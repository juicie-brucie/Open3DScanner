- 2026-10-19 — hardware (main session, `wt-hw`, branch `hw/next`): #73/#89's
  combined quit rerun, both fixes landed (PR #237, PR #236). The vendor
  context_destroy hang reproduced again (expected). **#73 confirmed fixed**:
  `windowWillClose during termination; ignoring (idempotent)` — no more
  double-firing; the app's own 120s deadline fired (`SIGKILL` the stuck
  worker, reply `NSTerminateNow`), exiting cleanly with **no orphan** (`ps`
  confirmed both processes gone). #73 → `DONE`. **#89 stays open**: its
  own 5s detached-thread bound around `obscan_context_destroy()` never
  showed any of its diagnostic lines (`TIMED OUT`/`ABANDONED`/`Exit code 4`)
  — the worker was still alive 119s after entering that code path, resolved
  only by #73's blunter outer kill. Either the 5s mechanism has a bug or
  never engages as written; needs an entry-point print to tell which.
