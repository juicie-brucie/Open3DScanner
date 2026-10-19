- 2026-10-18 — hardware + GUI (main session + the user, `wt-hw`, branch
  `hw/next`): #73's instrumented rerun (#79's logging landed). Settles
  into two distinct, precisely-located bugs. **Worker-side**: the log
  reaches "starting context_destroy poll loop" and then nothing, ever —
  not even one per-10th-poll "refused" line — meaning the vendor's
  `obscan_context_destroy()` hung on its very first call, not in our
  retry loop. Confirmed: the worker stayed alive at 287%+ CPU for
  minutes, killed manually. A third failure mode beyond the known crash
  and the bounded MULTI_CALLED pattern #43/#46 measured cleanly. Filed
  as **#81** (`TODO`). **App-side**: `applicationShouldTerminate` →
  `kill(SIGTERM)` → `NSTerminateLater` all correct, but 8.3s later a
  *different* path (`windowWillClose -> terminate:`) lets the process
  exit anyway, bypassing the wait and abandoning the now-permanently-
  stuck worker; that line logs twice, suggesting non-idempotent
  re-entry. #73 narrows to this app-side bug specifically — stays `HW`.
