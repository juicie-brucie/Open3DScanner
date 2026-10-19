# PLAN #73 — instrumented quit rerun (#79's instrumentation)

**Date**: 2026-10-18 (late evening)
**Operator**: main session + the user watching the window. Launch: terminal-redirect
(`OpenScanner.app/Contents/MacOS/OpenScanner 2>&1 | tee ...`), so both the app's stdout
and the new `/tmp/openscanner-quit.log` instrumentation (#79, PR #222) were captured.
Branch `hw/next`, HEAD includes #79's instrumentation merge. Clean `cmake --build`
beforehand; binary mtimes newer than source.

**Note on provenance**: the quit in this run was the user's own real ⌘Q keypress (done
slightly ahead of the planned script — see the session transcript), not a scripted
`osascript` quit. This is, if anything, more representative of the real failure mode than
a scripted quit.

## What the instrumented log shows — the two bugs, now precisely located

`/tmp/openscanner-quit.log` (archived as `2026-10-18-plan73-quit-instrumented.log`),
verbatim, with my own analysis annotations removed (the raw file has no comments):

```
1792374470.855418 [app pid 83291] applicationWillFinishLaunching
1792374471.005119 [app pid 83291] applicationDidFinishLaunching
1792374471.575821 [app pid 83291] worker launched pid=83309 path=.../scan_server
1792374471.663548 [worker pid 83309] RunLiveServer: installing signal handlers
1792374508.725490 [app pid 83291] applicationShouldTerminate entered; worker=0x7c7a192d50 running=1
1792374508.725656 [app pid 83291] kill(SIGTERM, pid=83309) -> 0 errno=2
1792374508.725665 [worker pid 83309] signal handler entered (g_stop_requested set)
1792374508.725702 [app pid 83291] returning NSTerminateLater
1792374508.747199 [worker pid 83309] live loop observed stop request -> breaking to teardown
1792374514.502743 [worker pid 83309] teardown: calling obscan_scan_stop(is_block=false)
1792374514.502778 [worker pid 83309] teardown: obscan_scan_stop returned
1792374515.073022 [worker pid 83309] teardown: session_destroy poll loop done
1792374515.073068 [worker pid 83309] teardown: starting context_destroy poll loop
1792374523.012362 [app pid 83291] windowWillClose -> terminate:
1792374523.013342 [app pid 83291] applicationWillTerminate (process about to exit)
1792374523.013584 [app pid 83291] windowWillClose -> terminate:
```
(end of file — no further lines, ever, over several subsequent minutes of observation)

### Bug A (worker-side): `obscan_context_destroy()` hangs indefinitely, not just its known crash

`tools/scan_server/main.cpp:1282` logs `"teardown: starting context_destroy poll loop"`,
then the loop's body (`main.cpp:1289`) calls `lib.obscan_context_destroy(&context)` for
the *first* time. The per-10th-poll print (`main.cpp:1293`, `"obscan_context_destroy poll
N: refused (...)"`) never appears even once — not even poll 1 — which means **the very
first call to the vendor's `obscan_context_destroy()` never returned at all**; this is not
the loop retrying against repeated `MULTI_CALLED` refusals (which would print), it is a
single call blocked inside the vendor SDK indefinitely.

Confirmed by direct process observation: the worker (PID 83309) was still alive several
minutes later, consuming 287%+ CPU with 3+ minutes of accumulated CPU time, with **no**
further line ever appearing in either the quit log or the captured stdout
(`2026-10-18-plan73-app-stdout.log`, which ends at `obscan_session_destroy -> OB_SCAN_SUCCESS (0)`
— line 3046, nothing after). This worker was killed manually (`kill -9`) by this session to
end the run; it was not observed to resolve on its own.

This is a **third failure mode** for this already-known-hazardous vendor call, distinct from
both documented cases on record: (a) the README's known `SIGSEGV` crash, and (b) the
`MULTI_CALLED`-refuse-then-succeed pattern measured cleanly in `#43`/`#46`'s acceptance runs
(where `context_destroy` typically resolved in under 2s, 0-1 refusals). Here it neither
crashed nor resolved — it hung on the very first call, consuming CPU (busy-spinning inside
the vendor's own code, not in anything this codebase controls) indefinitely.

### Bug B (app-side): a second termination path bypasses the `NSTerminateLater` wait

`applicationShouldTerminate` correctly returned `NSTerminateLater` at `508.725702` and
should only let the process actually exit once the worker's `NSTask.terminationHandler`
fires (which never happens here, since the worker never exits). Instead, **8.3 seconds
later**, a *separate* code path fires: `windowWillClose -> terminate:` at `523.012362`,
followed immediately by `applicationWillTerminate` at `523.013342` — meaning the app's
process actually exited here, bypassing the wait entirely. `windowWillClose -> terminate:`
then logs a **second time** at `523.013584` (after the app was already mid-terminate),
suggesting the window-close path itself may have fired more than once or is not
idempotent/guarded against re-entry.

The net effect: the window visually closing (which the user confirmed — their real ⌘Q
"stopped the scanning, turned the lasers off" (that's `obscan_scan_pause`/`obscan_scan_stop`
succeeding, both logged) "but didn't close the app" at first) does not mean the worker was
actually torn down — in this run the app process itself did eventually exit via the
window-close path, but the worker it left behind was genuinely, permanently stuck, not
just slow.

## Reconciling with the prior (uninstrumented) rerun

The previous rerun (`2026-10-18-plan65-app-launch-rerun.md`, corrected per PR #220's
verdict) found a complete, clean teardown through `session_destroy` and guessed the
7-minute, 270%+-CPU process might be "a stale worker, or this one hanging after its clean
teardown." This run settles it precisely: it is **the same worker**, hanging **inside
`context_destroy`**, specifically on the vendor's call itself (not our own retry loop) —
not a stale/orphaned-but-otherwise-idle process, and not a hang *after* a clean teardown;
`context_destroy` is the one step that did not complete.

## Verdict

**Two distinct, now-precisely-located bugs**, not one:
1. `obscan_context_destroy()` itself can hang indefinitely on this hardware/session shape,
   a new failure mode beyond the documented crash and the previously-measured bounded
   `MULTI_CALLED` retry pattern. This is vendor-SDK behavior, likely not directly fixable
   from this codebase — candidate mitigations: a bounded timeout around this specific call
   (accepting an unclean-but-bounded exit if the vendor call itself never returns), or
   further recon into what distinguishes this hang from the runs where the same call
   resolved in under 2s.
2. The app's termination logic has a second, non-idempotent path
   (`windowWillClose -> terminate:`) that can let the process exit without the worker ever
   confirming its own exit — this one **is** directly fixable in `tools/macos_app/main.mm`:
   either guard `windowWillClose` so it only calls `terminate:` once and routes through the
   same `applicationShouldTerminate` gate, or make the orphan-guard itself bound and kill
   the worker outright if the graceful path doesn't complete within some deadline instead of
   quietly abandoning it.

Files: this record; `2026-10-18-plan73-quit-instrumented.log` (the `/tmp/openscanner-quit.log`
instrumentation, verbatim); `2026-10-18-plan73-app-stdout.log` (the worker/app's own stdout,
verbatim, 3046+ lines, ending mid-teardown with no further output).
