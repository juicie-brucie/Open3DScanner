- 2026-10-18 — hardware + GUI (main session + the user watching the window,
  `wt-hw`, branch `hw/next`): #65's first-ever live launch of
  `OpenScanner.app`. 2 of 4 observations failed. **Cold start**: the page
  never loads — `scan_server`'s `FindUiDir()` resolves `ui/` relative to
  the process cwd, which defaults to `/` under a GUI launch (not the
  build dir a terminal launch gives it); every request 404s, forever,
  silently (a 404 is a successful WebKit navigation, so the retry path
  never engages). Filed as **#72**. **Crash surfaced**: PASS — exact
  alert text, correct signal-9 status, no hang. **Clean exit**: not
  tested. **Graceful quit**: FAIL, reproduced twice (once post-crash,
  once on a fully healthy streaming session) — ⌘Q/window-close never
  triggers the worker's SIGTERM teardown; force-quit required both
  times. A screen recording (reviewed via ffmpeg frame extraction, not
  staged) shows the app vanishing from Force Quit's own app list by the
  end, rather than appearing as hung-but-listed. Filed as **#73**. #65 →
  `BLOCKED` pending both fixes landing, then a rerun.
