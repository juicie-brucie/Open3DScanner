# PLAN #106 — natural session-end confirmation (post-#266 watchdog)

**Date**: 2026-10-19, early morning
**Operator**: main session (process-level) + user (watching the window, confirmed the
alert text and that the window stayed open after dismissal). Worktree `wt-hw`, branch
`hw/next`, HEAD `60df085` + this session's bundle fix for #107.

## Setup

Pre-seeded a short session with `defaults write org.openscanner.app
OpenScannerSessionSeconds 20`, launched fresh (app PID 29904, worker PID 29910, `--live-worker
8787 ... 20`), and let it run to its own natural end — no quit requested, no `osascript`,
nothing. This is the exact path #106 was filed against: the worker's own session-length
timeout drives the same teardown as a quit, but previously had no deadline watching it at
all once it hit the vendor hang.

## Result

`/tmp/openscanner-quit.log` (archived as `2026-10-19-plan106-natural-end-confirmed.log`):

```
1792392573.094897 [app pid 29904] applicationWillFinishLaunching
1792392573.139777 [app pid 29904] applicationDidFinishLaunching
1792392573.320501 [app pid 29904] worker launched pid=29910 path=.../scan_server
1792392573.333313 [worker pid 29910] RunLiveServer: installing signal handlers
1792392606.174775 [worker pid 29910] teardown: calling obscan_scan_stop(is_block=false)
1792392606.174800 [worker pid 29910] teardown: obscan_scan_stop returned
1792392606.541998 [worker pid 29910] teardown: session_destroy poll loop done
1792392606.542038 [worker pid 29910] teardown: starting context_destroy poll loop
1792392606.542064 [worker pid 29910] teardown: context_destroy: poll 1: entered the 5000 ms bound (before any allocation)
1792392606.542072 [worker pid 29910] teardown: context_destroy: poll 1: state allocated; spawning the vendor call
1792392606.542101 [worker pid 29910] teardown: context_destroy: poll 1: vendor call thread spawned; wait started (5000 ms)
1792392611.566352 [worker pid 29910] teardown: context_destroy: poll 1: wait TIMED OUT after 5000 ms; abandoning context (not joined)
1792392611.566389 [worker pid 29910] teardown: context_destroy: ABANDONED after 5.0s (context left for process exit to reclaim)
1792392611.566395 [worker pid 29910] teardown: context_destroy -> ABANDONED (no vendor return); exiting 4
1792392611.566399 [worker pid 29910] teardown: context_destroy poll loop done
1792392611.599734 [worker pid 29910] teardown complete
1792392611.599931 [worker pid 29910] RunLiveServer returning (worker about to exit)
1792392611.599940 [worker pid 29910] teardown: Exit code 4 = context_destroy deadline exceeded (distinct from 2 = skipped/no dylib)
1792392611.615836 [app pid 29904] worker terminationHandler: pid=29910 status=4 reason=1
1792392611.615912 [app pid 29904] terminationHandler on main queue; quitting=0 restarting=0
```

No further trace lines (expected — `quitting=0 restarting=0` falls to `surfaceError`,
`tools/macos_app/main.mm:200-202`, which doesn't call `QuitTrace`).

The vendor hang reproduced on this path too (same underlying vendor bug, unconditional on
why teardown started), but the worker's own 5s bound caught it and exited with code 4 —
same as #89's quit-path confirmation, ~5.0s from entering the bound to abandoning. The
app's `terminationHandler` fired within ~16ms of the worker's exit and, since this wasn't
`quitting`/`restarting`, correctly fell to the `else` branch (status != 0): `surfaceError`,
a sheet reading **"Capture worker exited unexpectedly (status 4)"**. The user confirmed
seeing this exact alert and dismissing it; the window stayed open afterward (correct — a
non-quit error shouldn't force the app closed). `ps` afterward: app process (29904) alive
and idle with the alert dismissed, consistent with design; this session's own `kill`
closed it out to clean up, not an observed bug.

An `osascript`/System Events window-list query came back empty for this app's window while
it was confirmed visible on screen — a scripting-terminology limitation of this app's
WebKit-backed `NSWindow` (`has scripting terminology: false` per `System Events`'s own
properties dump), not a real missing window. Noted here only so a future run doesn't
mistake that tooling gap for a hang.

## Verdict

**#106 confirmed resolved.** Before #266, this path hung indefinitely and silently (HUD
stuck on `status: connected`, zero capture, no feedback). Now: the worker is bounded
regardless of why teardown started (quit or natural end), exits within ~5s with a
distinguishable code, and the app surfaces a clear, actionable error rather than hanging.
The `restartWorker`/settings path (same `self.restarting` flag, noted during #65 as
sharing this gap) is covered by the same reasoning — the fix is in the worker's own call,
not in which caller waits on it — but was not separately exercised this run. `HW` → `DONE`.

Files: this record; `2026-10-19-plan106-natural-end-confirmed.log` (quit-log trace);
`2026-10-19-plan106-natural-end-confirmed-stdout.log` (worker stdout).
