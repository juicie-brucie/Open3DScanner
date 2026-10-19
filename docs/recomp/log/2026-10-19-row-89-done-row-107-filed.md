- 2026-10-19 — hardware (main session, `wt-hw`, branch `hw/next`): **#89 confirmed
  fixed.** PR #266's bounded watchdog resolves the vendor `obscan_context_destroy()`
  hang in ~11s (caller's own 5s wait catches it, worker exits with the new code 4) vs.
  the prior ~126s outer-deadline `SIGKILL`. `ps` confirmed no orphan. `HW` → `DONE`.
  First attempt against this was a **false miss**: the app bundle's `scan_server` was
  stale — built before #266 landed — because `tools/macos_app/CMakeLists.txt`'s
  `POST_BUILD` bundle-copy step is tied to `OpenScanner`'s own link rule and doesn't
  re-fire when only `scan_server` rebuilds. Filed as **#107** (`TODO`). Worked around
  this run with a manual `cp` + re-`codesign`, gated by a `strings` check for the new
  trace text before testing — worth keeping as a standing pre-test sanity check until
  #107 lands properly.
