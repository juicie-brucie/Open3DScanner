# PLAN #89 — bounded_watchdog (PR #266) live-quit confirmation

**Date**: 2026-10-19, early morning
**Operator**: main session (direct, no GUI interaction needed for this one — process-level
quit via `osascript`). Worktree `wt-hw`, branch `hw/next`, HEAD `60df085` (PR #269 merged).

## First attempt: a miss, caused by a stale binary — not the watchdog

The first rerun against what I believed was a freshly-built binary reproduced the *exact*
pre-#266 failure: `/tmp/openscanner-quit.log` went straight from `"starting context_destroy
poll loop"` to the old outer 120s deadline's `"termination deadline reached; SIGKILL"`, with
none of #266's per-poll tracing in between (archived as
`2026-10-19-plan89-watchdog-confirm-miss.log`).

Root cause: `tools/macos_app/CMakeLists.txt`'s `POST_BUILD` step that copies `scan_server`
into the app bundle (lines 51-59) is attached to the `OpenScanner` *executable's own link
step*. Since `main.mm` hadn't changed, CMake considered the `OpenScanner` target already
up to date and never re-ran the `copy_if_different` for `scan_server`, even though
`scan_server` itself had rebuilt from `main.cpp`/`bounded_watchdog.h` minutes earlier.
`strings` on the bundled binary confirmed it: no `"WATCHDOG fired"` / `"entered the ... ms
bound"` text anywhere — the bundle was running a scan_server from *before* #266 landed
(mtime Oct 18 22:16, vs. source mtime Oct 19 02:40). A third, even staler copy exists at
`Contents/Resources/scan_server` (Oct 18 19:49) too.

This is a real build-system bug, independent of #89 — filed separately as **#107** below.
Fixed in place for this run only with a manual `cp` + re-`codesign` of the bundle (not a
durable fix; #107 owns that).

## Second attempt, correct binary: PASS

`strings` confirmed the watchdog's trace text is present before testing. Launched fresh
(PID 28600/28605), confirmed cold-start connectivity (`curl` 200), streamed ~40s, quit via
`osascript -e 'tell application "OpenScanner" to quit'`.

`/tmp/openscanner-quit.log` (archived as `2026-10-19-plan89-watchdog-confirmed.log`),
verbatim:

```
1792392409.361987 [app pid 28600] applicationShouldTerminate entered; worker=0x7cff8435c0 running=1 quitting=0
1792392409.362044 [app pid 28600] kill(SIGTERM, pid=28605) -> 0 errno=2
1792392409.362052 [worker pid 28605] signal handler entered (g_stop_requested set)
1792392409.362073 [app pid 28600] returning NSTerminateLater
1792392409.403780 [worker pid 28605] live loop observed stop request -> breaking to teardown
1792392415.033579 [worker pid 28605] teardown: calling obscan_scan_stop(is_block=false)
1792392415.033662 [worker pid 28605] teardown: obscan_scan_stop returned
1792392415.392983 [worker pid 28605] teardown: session_destroy poll loop done
1792392415.393053 [worker pid 28605] teardown: starting context_destroy poll loop
1792392415.393090 [worker pid 28605] teardown: context_destroy: poll 1: entered the 5000 ms bound (before any allocation)
1792392415.393101 [worker pid 28605] teardown: context_destroy: poll 1: state allocated; spawning the vendor call
1792392415.393115 [worker pid 28605] teardown: context_destroy: poll 1: vendor call thread spawned; wait started (5000 ms)
1792392420.444682 [worker pid 28605] teardown: context_destroy: poll 1: wait TIMED OUT after 5000 ms; abandoning context (not joined)
1792392420.444730 [worker pid 28605] teardown: context_destroy: ABANDONED after 5.1s (context left for process exit to reclaim)
1792392420.444737 [worker pid 28605] teardown: context_destroy -> ABANDONED (no vendor return); exiting 4
1792392420.444741 [worker pid 28605] teardown: context_destroy poll loop done
1792392420.488171 [worker pid 28605] teardown complete
1792392420.488285 [worker pid 28605] RunLiveServer returning (worker about to exit)
1792392420.488295 [worker pid 28605] teardown: Exit code 4 = context_destroy deadline exceeded (distinct from 2 = skipped/no dylib)
1792392420.506429 [app pid 28600] worker terminationHandler: pid=28605 status=4 reason=1
1792392420.506491 [app pid 28600] terminationHandler on main queue; quitting=1 restarting=0
1792392420.506523 [app pid 28600] replying NSTerminateNow (worker exited)
1792392420.509916 [app pid 28600] applicationWillTerminate (process about to exit)
1792392420.513555 [app pid 28600] windowWillClose during termination; ignoring (idempotent)
```

The vendor's `obscan_context_destroy()` hung again (same underlying vendor bug, unchanged —
this run doesn't fix that), but this time the **caller's own 5s wait** (not even the
watchdog's `on_fire`, which never needed to trigger) noticed, abandoned the call, and the
worker exited cleanly with the new dedicated code **4** ("context_destroy deadline
exceeded"). Total time from `kill(SIGTERM)` to worker exit: **~11.1s** (`409.36` to
`420.49`), vs. the ~126s outer-deadline SIGKILL path measured on #73/#89's prior runs.
`ps aux` immediately after: zero OpenScanner/scan_server processes — no orphan.

## Verdict

**#89 confirmed fixed.** The bounded per-call wait (not strictly the watchdog thread's
`on_fire`, which is the backstop for a caller that's *itself* stuck — see #107's note below
on why that distinction matters) resolves the hang in ~11s instead of ~126s, with a clean,
distinguishable exit code and no orphaned worker. Status → `DONE`.

Open note: this run never exercised the watchdog thread's own `on_fire` firing (the normal
caller-side 5s wait always won the race). That path is real — this binary's `bounded_watchdog_test`
unit tests cover it directly — but it remains unconfirmed against actual hung hardware in this
run. Not blocking; the caller-side bound already achieves the goal.

Files: this record; `2026-10-19-plan89-watchdog-confirm-miss.log` (the stale-binary miss,
kept for the build-bug record); `2026-10-19-plan89-watchdog-confirmed.log` (the real pass);
`2026-10-19-plan89-watchdog-confirmed-stdout.log` (worker stdout for the passing run).
