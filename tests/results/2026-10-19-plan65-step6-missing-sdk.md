# PLAN #65/#66 — step 6 (missing-SDK dialog)

**Date**: 2026-10-19
**Operator**: main session + user watching the window.

## What happened

Launched with `OPENSCANNER_LIBRARY=/nonexistent/path/liborbbec_scan.dylib` set. `ps`
confirmed only the app process spawned — **no worker process at all**, consistent with
`resolveLibraryPath`/`startWorker` surfacing the error before ever calling
`resolveWorkerPath`/spawning `NSTask`. This path carries none of #106's teardown-deadline
risk since no worker ever exists to hang.

User's screenshot — the dialog read exactly:

```
OpenScanner

OPENSCANNER_LIBRARY is set but does not exist:

  • /nonexistent/path/liborbbec_scan.dylib

Correct it, or unset OPENSCANNER_LIBRARY to use /Applications/CrealityScan.app/Contents/PlugIns/liborbbec_scan.dylib.

[OK]
```

Matches the spec's expectation precisely: names the exact override path that was probed
(not a path that wasn't checked), and names the correct fallback. No crash, no opaque
failure, no worker spawned to clean up.

## Verdict

**PASS.** #74's fix (the set-but-missing override being fatal rather than silently
skipped) works exactly as designed.
