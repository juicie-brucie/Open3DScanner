# PLAN #65 — rerun, after #72/#73/#74's fixes landed

**Date**: 2026-10-18 (evening)
**Operator**: main session, with the user watching the actual window and relaying its
content (this session has no screen/desktop control tool). Launches driven directly from
the terminal this time (`.../OpenScanner.app/Contents/MacOS/OpenScanner 2>&1 | tee ...`),
per the spec's own guidance for capturing the worker's stdout — plus a check that the app
actually opens a normal, window-server-connected window (it does; the user saw and
interacted with it), addressing the prior rerun's note that the hang might have been
specific to a launch without a window-server connection.

Branch `hw/next`, HEAD includes PR #202 (#72, `FindUiDir` relative-to-binary fix), PR #203
(#73, "reliable quit path; non-modal errors; orphan guard"), and PR #205 (#74, the
set-but-missing `OPENSCANNER_LIBRARY` override fix) — confirmed via `git log`/
`git merge-base --is-ancestor` before starting, and a clean `cmake --build` beforehand.

## Observation 1 — Cold start: **PASS**

Launch A, terminal-redirect method. `curl` probe: attempt 1 = `000` (not yet listening),
attempt 2 (~2s later) = `200`. `curl -s http://127.0.0.1:8787/ | head -3` returned real
page markup (`<!doctype html>...`), not a 404 — #72's fix holds under this launch method.

User's screenshot confirms: title `"OpenScanner — port 8787, session 300 s"` (A4's title
format, #192), and the page fully renders: `backend: webgpu`, `status: connected`,
`scan: scanning (live)`, `render: 60.0 fps`, `data: 6.0 fps`, points climbing (195 at
frame #105). Full pass — page loads, HUD live, V5's WebGPU backend active by default.

## Observation 2 — Crash surfaced: **PASS**

Killed the worker directly (`pgrep -fl live-worker` → `kill -9 <pid>`). User's screenshot:
title changed to `"OpenScanner — worker not running"`; alert read exactly
`"OpenScanner / Capture worker exited unexpectedly (status 9)."` — correct signal-9
report. User clicked OK; app stayed open and responsive (per the user: "clicked okay and
the app is staying open"). Matches the spec's expectation exactly.

Quitting this instance (worker already dead) via `osascript -e 'tell application
"OpenScanner" to quit'` exited the app process immediately — `ps` confirmed empty right
after. This is the `NSTerminateNow` branch (nothing to tear down) and worked as expected —
**not** the case that matters for observation 4 below.

## Observation 3 — Clean exit: **NOT TESTED THIS RUN**

Not attempted — superseded by the priority of re-testing observation 4 (the quit path)
under a real window-server-connected launch, which observation 2's instance couldn't
provide (worker was already dead). Left for a follow-up.

## Observation 4 — Graceful quit (⌘Q / window-close → SIGTERM → teardown): **FAIL — a new, different failure mode**

Fresh launch (Launch B, terminal-redirect, log captured to this record's companion `.log`).
Connected on attempt 1 (`curl` → `200` immediately — device/session warm from the prior
run). Let it stream for 5s, then quit was requested via `osascript -e 'tell application
"OpenScanner" to quit'` at `21:01:09`.

**What happened, precisely, in this order:**

1. `osascript` returned immediately (no error, no hang on the script side).
2. Polling `ps -p <app-pid>` every 1s for 20s: the app's own process (PID 59609) was
   **already absent from the very first check (t+1s)** and stayed absent for the rest of
   the poll — the app's own process exited almost instantly.
3. The user, watching the actual window, reports: **the window stayed visually present
   and the app appeared to be hanging** at this point — i.e. the on-screen state did not
   match what `ps` showed (a vanished process).
4. The user then pressed **⌘Q themselves, separately, after observing the hang** — this
   **did not close the window either**.
5. The user then **clicked the window's own red traffic-light close button** — this
   **did** make the window disappear, finally.
6. Checked `ps` again at this point (and repeatedly over the following several minutes):
   the worker (`scan_server --live-worker`, PID 59613) was **still running**, actively
   consuming **270%+ CPU**, for a measured **7+ minutes** past the original quit request,
   until manually `kill -9`'d by this session. No teardown (`pause`/`stop`/`session_destroy`/
   `context_destroy`) ever appears in the captured log for this run.
7. No macOS crash report was generated for the app (`~/Library/Logs/DiagnosticReports/`
   checked, no `OpenScanner*.ips` present) — whatever happened to the app's own process
   was not a traditional SIGSEGV/SIGABRT-style crash.
8. The captured stdout log (`2026-10-18-plan65-app-launch-rerun.log`) ends abruptly
   mid-print, inside a Vulkan/MoltenVK extension enumeration block (WebGPU backend
   initialization output from the page's own renderer) — no further lines, no app-level
   exit message, no teardown sequence. Consistent with (not proof of) the process's
   stdout pipe closing non-orderly, rather than an application-level clean shutdown
   printing its own final lines.

**Read**: this is not the same failure as the original #73 finding (where the app never
responded at all and persisted, listed as hung, until a Force Quit). Here, the app's own
process disappears quickly and without a crash report — but (a) the window remains a
stale, unresponsive visual artifact that neither the original quit request nor a
follow-up ⌘Q can dismiss, only a direct click on its close button, and (b) the worker is
definitively orphaned, not momentarily delayed — 7+ minutes and climbing when checked,
pulled by continuous CPU usage, not blocked waiting on anything. The orphan-guard #73
was supposed to add either isn't triggering on this path or isn't reaching the worker.

This directly contradicts #73's "not reproduced outside the room" diagnosis, which
attributed the original symptom to "a launch without a window-server connection" — this
rerun used a normal, window-server-connected launch (the user watched and interacted with
a real window) and still produced an orphaned worker, just with different surface
symptoms than the first report.

## Observations 5–6 (A4's settings / missing-SDK dialog): **NOT TESTED THIS RUN**

Not reached — the quit-path finding in observation 4 took priority and ended this test
session's instance. Left for a follow-up once #73 is actually fixed (or at least once
someone can attach a debugger to the quit path, since black-box `ps`-level observation
can't show why the SIGTERM either isn't sent or isn't landing).

## Summary

| # | Observation | Result |
|---|---|---|
| 1 | Cold start | **PASS** — #72 confirmed fixed under terminal launch |
| 2 | Crash surfaced | **PASS** — exact alert, correct status, responsive |
| 3 | Clean exit | not tested |
| 4 | Graceful quit | **FAIL, new variant** — app process vanishes quickly (no crash report) but the window needs a manual close-button click, and the worker is left running orphaned for 7+ minutes, no teardown ever logged |
| 5 | Settings (A4) | not tested |
| 6 | Missing-SDK dialog (A4) | not tested |

## Files
- `tests/results/2026-10-18-plan65-app-launch-rerun.log` — captured stdout/stderr from
  Launch B (the quit-test instance); ends abruptly as described in observation 4.
- No crash report exists for this run (checked, not assumed).
