# PLAN #73 + #89 — combined quit rerun, after both fixes landed

**Date**: 2026-10-19
**Operator**: main session. Launch: terminal-redirect
(`OpenScanner.app/Contents/MacOS/OpenScanner 2>&1 | tee ...`), with the #79 quit-log
instrumentation (`/tmp/openscanner-quit.log`) also captured. Branch `hw/next`, HEAD
includes PR #237 (#73's idempotent-quit + 120s app-level deadline) and PR #236 (#89's
5s bounded, detached-thread wrapper around `obscan_context_destroy()`) — confirmed via
`git merge-base --is-ancestor` before starting. Clean `cmake --build` beforehand.

## Sequence

1. Fresh launch (app PID 84802, worker PID 84809). Connected on `curl` attempt 2.
2. Let it stream ~5s, then quit via `osascript -e 'tell application "OpenScanner" to
   quit'`.
3. **The `osascript` call itself did not return for 2 minutes** (hit this session's own
   tool timeout) — a surprise going in, but turned out to be expected: see below.
4. After the timeout, `ps` showed the worker still alive (313%+ CPU, 6+ minutes
   accumulated), consistent with the known `obscan_context_destroy()` vendor hang
   (#89's underlying symptom). Checked again shortly after: **both processes were
   gone**. No manual kill was needed this time.

## The quit log, verbatim (`/tmp/openscanner-quit.log`, archived as
`2026-10-19-plan73-89-quit-rerun.log`)

```
1792387388.105762 [app pid 84802] applicationWillFinishLaunching
1792387388.155638 [app pid 84802] applicationDidFinishLaunching
1792387388.448671 [app pid 84802] worker launched pid=84809 path=.../scan_server
1792387388.499141 [worker pid 84809] RunLiveServer: installing signal handlers
1792387405.649447 [app pid 84802] applicationShouldTerminate entered; worker=0x774a08d040 running=1 quitting=0
1792387405.649542 [app pid 84802] kill(SIGTERM, pid=84809) -> 0 errno=2
1792387405.649549 [worker pid 84809] signal handler entered (g_stop_requested set)
1792387405.649617 [app pid 84802] returning NSTerminateLater
1792387405.706296 [worker pid 84809] live loop observed stop request -> breaking to teardown
1792387411.946410 [worker pid 84809] teardown: calling obscan_scan_stop(is_block=false)
1792387411.946454 [worker pid 84809] teardown: obscan_scan_stop returned
1792387412.420257 [worker pid 84809] teardown: session_destroy poll loop done
1792387412.420293 [worker pid 84809] teardown: starting context_destroy poll loop
1792387531.641283 [app pid 84802] termination deadline reached; SIGKILL pid=84809
1792387531.641361 [app pid 84802] deadline: replying NSTerminateNow
1792387531.645666 [app pid 84802] applicationWillTerminate (process about to exit)
1792387531.646002 [app pid 84802] windowWillClose during termination; ignoring (idempotent)
1792387531.646186 [app pid 84802] worker terminationHandler: pid=84809 status=9 reason=2
```

Elapsed from `"starting context_destroy poll loop"` (`412.420293`) to the app's deadline
firing (`531.641283`): **119.2 seconds** — matching #73's documented 120s app-level
deadline almost exactly.

## Reading it: two things confirmed, one thing not

**#73 — CONFIRMED FIXED.** Both of its specific symptoms from the prior rerun are gone:
- `windowWillClose during termination; ignoring (idempotent)` — the non-idempotent
  re-entry bug (the duplicate `windowWillClose -> terminate:` line from the prior rerun)
  is fixed; it now logs once and is explicitly guarded.
- The app's own 120s deadline fired, `SIGKILL`ed the stuck worker itself
  (`termination deadline reached; SIGKILL pid=84809`), replied `NSTerminateNow`, and the
  app terminated cleanly via `applicationWillTerminate` — **no orphan**: `ps` confirms
  both processes gone, not just the app. This is the critical property #73 was filed for
  (a stuck worker must not be silently abandoned forever) and it now holds.

**#89's specific 5-second inner bound — NOT confirmed to have fired.** The fix
(`tools/scan_server/main.cpp` ~1296-1340) wraps the vendor `obscan_context_destroy()`
call in a detached thread with its own 5000ms wait, and on timeout should print
`"[worker] obscan_context_destroy: TIMED OUT after 5000 ms on poll 1; abandoning
context (not joined)"`, then shortly after `"ABANDONED after ...s"`, then eventually
`"Exit code 4 = context_destroy deadline exceeded"` and return exit code 4 from the
worker's own `main()`. **None of these lines appear anywhere** — not in the quit log
(the `QuitTrace("teardown: context_destroy poll loop done")` call that should follow the
5s-bound block never logs either) and not in the captured stdout
(`2026-10-18-plan73-rerun2.log`'s tail, grepped for all of `context_destroy`,
`TIMED OUT`, `ABANDONED`, `RunLiveServer returning`, `Exit code 4` — zero matches). The
worker process was still alive and CPU-bound a full 119 seconds after entering that
block — far past the 5-second bound the fix should have enforced on its own.

So what actually ended this run was **#73's blunter, outer 120s app-level `SIGKILL`**,
not #89's finer-grained 5s detached-thread abandon-and-continue logic. The worker
process itself never got the chance to print its own `ABANDONED`/exit-4 path and exit
semi-gracefully on its own terms — it was just forcibly killed from outside before that
could happen (or before it *would* have happened, if it was going to at all; this run
can't tell which).

## Verdict

- **#73: PASS.** Idempotent quit handling confirmed; the 120s deadline safety net works
  exactly as intended — no orphan, clean app exit, worker reliably reaped even when the
  vendor call never returns.
- **#89: INCONCLUSIVE on the specific mechanism.** The underlying vendor hang
  reproduced again (expected — this is vendor SDK behavior, not something either fix
  touches). But the 5-second detached-thread bound that's supposed to let the *worker*
  recognize the hang and exit on its own (code 4) within ~5s was never observed to fire
  — the run was resolved by #73's outer 120s kill instead, ~115 seconds later than #89's
  own bound should have resolved it. Either #89's bound has a bug that prevents it from
  ever completing its own abandon-and-continue path, or something about it is masked/
  lost in this specific run. Needs its own investigation — recommend adding a print
  immediately upon entering the 5s-wait loop (before anything else) to rule out the
  block not being reached at all, versus being reached but not completing its own
  timeout-detection logic correctly.
