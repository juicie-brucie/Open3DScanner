# PLAN #66 — `A4` settings live checks (port-change, session-length)

**Date**: 2026-10-19
**Operator**: hw-test subagent (no live human watcher this run — see Limitations).
Worktree `wt-hw`, branch `hw/next`, HEAD `009a199`.
Binary freshness: `build/.../OpenScanner.app/Contents/MacOS/OpenScanner` mtime Oct 19
14:25, newer than `tools/macos_app/main.mm` (Oct 18 22:16); `cmake --build build
--target OpenScanner` immediately before the run showed every target `Built target …`
— no rebuild fired, confirming the bundle already matched source.

## Why this run

Row #66 (`docs/recomp/PLAN.md` line 76): the remaining owed checks (port change,
session length) ride the #65 launch-run acceptance (`tests/hardware/plan65-app-launch.md`
step 5) and were blocked on #106. #106 is DONE (confirmed
`tests/results/2026-10-19-plan106-natural-end-confirmed.md`). Step 6 (missing-SDK) was
already PASS (`2026-10-19-plan65-step6-missing-sdk.md`). This run covers step 5 only.

## Preconditions checked

- `pgrep -x scan_server`, `-x live-worker`, `-x raptor_client`, `-x OpenScanner`: all
  empty before starting — no conflicting SDK user.
- Found an unrelated stray process: PID 87733, `/bin/sh /tmp/stubborn_worker.sh
  --live-worker 8787 /bin/ls 300` (started Oct 18 21:57, PPID 1/orphaned). Its script
  is a `sleep`-loop mock that ignores SIGTERM; `lsof -iTCP:8787` showed it was **not**
  actually bound to that port. It never touched the real SDK or device, so it did not
  block this run, but it is leftover state from an unrelated test and is flagged here,
  untouched, for whoever owns cleanup.
- `defaults read org.openscanner.app` was empty — clean settings state, no leftover
  `OpenScannerSessionSeconds` from #106's run.
- No crash reports in `~/Library/Logs/DiagnosticReports/` newer than 14:40 (checked
  `-newermt`); the two most recent `scan_server-*.ips` predate this run (14:28–14:29).

## Procedure and observations

Launched `OpenScanner.app/Contents/MacOS/OpenScanner` from a terminal (captured stdout,
raw log `2026-10-19-plan66-a4-settings.log`), confirmed the real device connected and
streaming (`FRAME_READY`, `tracking_status`), default port 8787 / session 300s, worker
pid 64610.

**Settings dialog (Cmd-,)**: opened via `System Events` accessibility (not a screenshot
— see Limitations); `entire contents of window 1` enumerated exactly: title
"OpenScanner Settings", informative text "Applied on OK; the worker restarts and the
page reloads for a port change. Quit always runs the full teardown.", a "Port" field, a
"Session (s)" field, Cancel/OK buttons — byte-for-byte the spec's described dialog.

**Port + session-length change**: set Port field to `8788`, Session field to `20` via
accessibility (`set value of text field`). The OK button fired on its own — setting the
session field's value triggered the field's Return/default-button action before an
explicit click command ran (the click then errored "Can't get button OK…" because the
dialog was already gone — benign, a side-effect of the automation method, not a bug).

Trace (`openscanner-quit.log`, copied as `*-quittrace.log`) confirms the full chain:
```
...restarting=1 ... worker terminationHandler: pid=64610 status=4 reason=1
... worker launched pid=65252 ... --live-worker 8788 .../liborbbec_scan.dylib 20
```
The old worker (8787) got SIGTERM via `restartWorker`, ran the normal
pause/stop/session_destroy chain, then hit the **same vendor `context_destroy` hang**
as #89/#106 (5s bound, `ABANDONED`, exit 4) — but because `self.restarting` was true,
the app correctly suppressed the error alert and relaunched on the new port/session
(`--live-worker 8788 ... 20`), per design. `curl 127.0.0.1:8788/` → `200`; the new
worker's log showed `FRAME_READY tracking_status=0 (SUCCESS)` shortly after — the page
reconnected on the new port. **PASS**: port-change reload + restart chain confirmed live.

**Session length applied**: worker launched with `20` as its `--live-worker` seconds
argument — confirmed applied. **PASS**.

**Natural session end (20s)**: ~36s later (consistent with session length + mesh-flush
overhead seen in #106), the second worker hit the **same vendor hang again** (exit 4).
Since `restarting=0` this time, the app took the `else` branch: `surfaceError`
("Capture worker exited unexpectedly (status 4).") and title → "OpenScanner — worker
not running" — **not** the spec's illustrative "… ended (20 s)" title, because that
branch requires a clean status-0 return, which the vendor's `context_destroy` hang
never gives on this hardware (now observed 3 for 3 across #89, #106, and this run).
This matches #106's own confirmed resolution exactly (the watchdog catches the hang and
surfaces the "exited unexpectedly" alert) — **not a new #66 defect**, but it does mean
the spec step 5's literal "confirm the title carries it (N s)" sub-step is not
reachable live on this hardware; recorded as an open question below rather than
fabricated as observed.

Dismissed the alert (Return key), confirmed the main window still existed (count 1) —
matching #106's documented quirk that `System Events` window enumeration returns empty
while an alert sheet is attached (not a real window loss). Quit via Cmd-Q: trace shows
`applicationShouldTerminate ... running=0 quitting=0` → `NSTerminateNow` immediately
(no worker to wait on) → clean process exit, confirmed via `pgrep -x OpenScanner`
afterward (empty).

## Checks

| # | Check | Result |
|---|-------|--------|
| 1 | Settings dialog (Cmd-,) shows exact spec text/fields | [PASS] |
| 2 | Port change restarts worker through SIGTERM→pause/stop/session_destroy/context_destroy chain | [PASS] |
| 3 | Page reloads/reconnects on new port (HTTP 200, frames flowing) | [PASS] |
| 4 | Session-length setting applied to the new worker's argv | [PASS] |
| 5 | Title literally reads "… ended (N s)" after natural end | [FAIL — not reachable; vendor `context_destroy` hang always fires, exit 4, "exited unexpectedly" alert instead; matches #106, not a new bug] |
| 6 | No crash report generated | [PASS] (none in `~/Library/Logs/DiagnosticReports/` for this window) |

## Verdict

**PASS** for #66's owed live checks (port-change + session-length), with one recorded
finding: the spec's "session ended (N s)" title text is aspirational and not observed
live — the already-known vendor `context_destroy` hang (#89/#106) always intervenes, so
the natural-end UI path always surfaces as "Capture worker exited unexpectedly (status
4)" instead. This is consistent with #106's own confirmed resolution and is not treated
as a #66 regression.

## Open questions

- Should the spec text (`tests/hardware/plan65-app-launch.md` step 5, "confirm the
  title carries it") be corrected to describe the exit-4/alert path instead of a
  clean-exit title, given the vendor hang now reproduces 3 for 3? Not fixed here per
  the "don't edit the test" rule — flagging for the plan/spec owner.
- The stray `/tmp/stubborn_worker.sh` (PID 87733) is unrelated leftover state; left
  untouched, flagged for cleanup by whoever owns it.

## Limitations

- No screen-recording permission was available to this run (`screencapture` failed:
  "could not create image from display"), so there is no visual screenshot evidence —
  unlike #106/step 6, which had a human watching. All UI-state claims here are
  evidence from `System Events` accessibility queries (window title/contents) cross-
  referenced against the app/worker's own trace log, not a screenshot. Flagging this
  so the record's confidence level is clear.

## Board move

#66: `HW` → `DONE` (the owed port-change and session-length checks are confirmed; the
one gap found is a spec-wording mismatch already explained by #106, not a new blocker).

## Files

- `tests/results/2026-10-19-plan66-a4-settings-confirmed.md` (this file)
- `tests/results/2026-10-19-plan66-a4-settings.log` (raw worker/app stdout)
- `tests/results/2026-10-19-plan66-a4-settings-quittrace.log` (QuitTrace timeline)
