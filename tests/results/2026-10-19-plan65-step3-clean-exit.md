# PLAN #65 — step 3 (clean exit), after #73/#89's quit-path work

**Date**: 2026-10-19
**Operator**: main session + user watching the window. Session length pre-seeded to 15s
via `defaults write org.openscanner.app OpenScannerSessionSeconds 15` (avoids a 5-minute
wait; no GUI interaction needed for this). Launch: terminal-redirect, quit-log captured.

## What happened

Launched fresh (app PID 90672, worker PID 90679, confirmed `--live-worker 8787 ... 15` in
its argv — the 15s override took). Connected on attempt 1; user confirmed the window
showed a live connection with data. Then, with **no quit requested by anyone**, the
worker's own 15s session timer should trigger the same teardown sequence
(`pause`/`stop`/`session_destroy`/`context_destroy`) that a user-initiated quit uses.

The quit log (`2026-10-19-plan65-step3-clean-exit.log`) shows exactly that — teardown
started on its own, no `applicationShouldTerminate` entry anywhere (confirming this was
the worker's own natural-end path, not an app-initiated quit):

```
1792388275.121833 [app pid 90672] applicationWillFinishLaunching
1792388275.199572 [app pid 90672] applicationDidFinishLaunching
1792388275.786400 [app pid 90672] worker launched pid=90679 path=.../scan_server
1792388275.942726 [worker pid 90679] RunLiveServer: installing signal handlers
1792388309.859923 [worker pid 90679] teardown: calling obscan_scan_stop(is_block=false)
1792388309.859960 [worker pid 90679] teardown: obscan_scan_stop returned
1792388312.185304 [worker pid 90679] teardown: session_destroy poll loop done
1792388312.185333 [worker pid 90679] teardown: starting context_destroy poll loop
```
(end of file — nothing more, ever)

Same vendor `obscan_context_destroy()` hang as #89's other reproductions. The user,
watching the window, reported it stayed showing a connection but data capture dropped to
essentially 0 — consistent with the pause/stop having already succeeded (streaming
stopped) while the worker sits stuck in `context_destroy`, with the WebSocket connection
itself apparently not yet torn down (the code only calls `server.Stop()` *after* the
context_destroy block completes, which it never reached).

**31 seconds after entering that block, with zero progress**, this session force-killed
both the worker and the app (`kill -9`) rather than wait indefinitely — there was no
reason to expect it would ever resolve on its own.

## The key finding: no deadline is armed for this path

`#73`'s fix added a 120-second deadline specifically inside `applicationShouldTerminate`
(the app-initiated quit sequence) that forcibly `SIGKILL`s a stuck worker if it doesn't
exit in time. **That deadline is not armed here** — this teardown was triggered by the
*worker's own* session-length timeout, not by the app calling `terminate:`. The app is
just waiting on `NSTask.terminationHandler`, which never fires, with no timeout logic
watching it at all in this specific path.

So: the "clean exit" observation isn't just untested — it's **currently broken with no
recovery**, and it's actually a worse position than the quit path was even before #73's
fix, which at least failed *visibly* (force-quit required). Here it would have hung
indefinitely with the only sign being a stalled HUD.

## Verdict

**FAIL — a new gap, not yet covered by either #73 or #89's fixes.** The underlying
vendor hang (#89, still unconfirmed-fixed anyway) is the proximate cause, but the
*missing safety net for the natural-end path specifically* is the actionable, fixable
part: `#73`'s deadline logic needs to cover worker exit generally (any path that waits on
`terminationHandler`), not just the app-initiated-quit branch.

Recommend filing this as a new row rather than folding into #73 (which is already marked
`DONE` for the quit-initiated case it was scoped to) or #89 (which is about the vendor
call itself, not the app's missing watchdog coverage).
