# PLAN #65 — `A3`'s live launch (the app lifecycle)

**Date**: 2026-10-18
**Operator**: main session (orchestrating) + the user (watching the actual window — this
session has no screen/desktop control tool, so the window-level observations below are the
user's own, relayed verbatim; everything else is verified independently from process state,
`curl`, and the SDK's own log file).

**Launch method — deviation from the spec**: launched via `open build/tools/macos_app/OpenScanner.app`,
not the terminal-redirect method the spec recommends (`.../MacOS/OpenScanner 2>&1 | tee ...`).
Consequence: the worker's own stdout/stderr go to `/dev/null` under `open` (confirmed via
`lsof -p <worker-pid>`, fds 1/2 → `/dev/null`), so none of the worker's own `[worker] ...`
print lines were captured for this run. Teardown/crash evidence below comes from process
state (`ps`), the vendor SDK's own log file (`ScannerStreamSDK.log`, separate from the
worker's stdout), and the app window itself. **A rerun via the terminal-redirect method would
capture the worker's own log and is recommended** if finer-grained evidence is wanted.

**Branch**: `hw/next`. No other SDK-using process running before each launch (checked via
`ps aux`).

## Observation 1 — Cold start: **FAIL** (new bug found, blocks everything downstream)

Launched fresh (`open .../OpenScanner.app`). The app and worker both spawned correctly
(confirmed via `ps aux`: `OpenScanner` PID + `scan_server --live-worker 8787 <dylib> 300`
PID). macOS's local-network permission prompt appeared (for GVCP/USB discovery) — unrelated,
granted.

`curl -i http://127.0.0.1:8787/` returned **`404 Not Found`** repeatedly, indefinitely (not a
transient cold-start gap — confirmed 404 persisting well past the expected ~20s device-wait
window). Root cause, found by reading source: `tools/scan_server/main.cpp`'s `FindUiDir()`
only checks paths **relative to the process's current working directory**
(`"ui"`, `"../ui"`, `"../../ui"`). When launched as a terminal command from the build
directory, cwd is the build tree and one of those resolves. When launched as a GUI app (via
`open`, Finder, Spotlight — the normal way a real user would launch `OpenScanner.app`),
**cwd defaults to `/`** (confirmed: `lsof -p <worker-pid> | grep cwd` → `/`). None of the
three relative candidates exist under `/`, so `FindUiDir()` falls through to its own
fallback `return "ui";` — still relative, still wrong — and `ServeStaticRequest` 404s every
single request, including `/` → `/index.html`.

Consequence for the app: a `404` is a **successful** HTTP navigation from WebKit's point of
view (not a network-level failure), so `didFailProvisionalNavigation` never fires and the
retry-with-backoff logic (`retryPageLoadSoon`) **never engages** — the page just loads blank
and stays blank forever. The user's window showed a blank white page under the title bar,
consistent with this. This is **not** the intended "first load refused, retry closes the
gap" behavior the spec describes — it's a silent, permanent failure with no retry and no
error surfaced to the user at all (worse than a visible error).

This is a real regression in row #61 (A1 — "serve `ui/` from the capture process"), which was
marked `DONE`: it works when run from a terminal in the build tree, but not when launched the
way a real user launches a `.app`. Filed as a new row (below)/flagged for reopening #61.

## Observation 2 — Crash surfaced: **PASS**

With the cold-start session still running (blank page, but worker alive and the device
attached per the SDK log), killed the worker directly: `kill -9 <worker-pid>`.

The user's window (screenshot relayed): title changed to **"OpenScanner — worker not
running"**, and an `NSAlert` appeared reading exactly:

> OpenScanner
> Capture worker exited unexpectedly (status 9).

`status 9` is the expected report of `SIGKILL` (9) — the worker was killed with `kill -9`, so
`NSTask.terminationStatus` correctly reflects the signal. The app stayed responsive (the
alert was clickable, no hang, no spinning cursor) — matches the spec's expectation exactly.

Clicking "OK" only dismisses the alert (by design — it does not quit the app); the app
process remained running afterward, confirmed via `ps`.

## Observation 3 — Clean exit (status-0, natural session end): **NOT TESTED**

Not attempted this run — the worker's hardcoded 300s (`--live-worker` fixed to `"300"` in
`startWorker`) means a natural clean exit needs a full 5-minute wait, and observation 4 (the
quit hang, below) made continuing with this instance moot. Left for a follow-up run, ideally
combined with the terminal-redirect launch method so the worker's own
`obscan_scan_pause`/`get_realtime_model_mesh` lines are visible.

## Observation 4 — Graceful quit (⌘Q → SIGTERM → teardown): **FAIL, reproduced twice** (the most important finding)

**First attempt** (same instance as observations 1–2, worker already dead from the `kill -9`):
the user tried quitting via ⌘Q and the window's close button. **Neither worked** — the app did
not terminate. A force-quit was required. Given the worker was already not-running at this
point, `applicationShouldTerminate`'s own logic (`if (self.worker != nil && self.worker.running)`)
should have taken the `NSTerminateNow` branch and quit **immediately** — it did not quit at
all, with or without delay.

**Second attempt** (fresh cold launch, worker healthy and actively streaming — confirmed via
`ps`: worker at ~290% CPU, and the SDK's own log showing a continuous stream of
`Pipeline.cpp:267 Frame received on pipeline!` lines with no gap, right up to the moment of
the quit attempt): the user pressed ⌘Q. **Did not work.** Waited 30+ seconds with no change —
both the app and worker processes remained fully alive and the worker kept actively
processing frames (confirmed via `ps` and by tailing `ScannerStreamSDK.log`, which showed
zero `stop`/`destroy`/`pipeline`-teardown-related lines in that window — teardown never
started). A force-quit was required again.

**This is not an edge case specific to the post-crash state** — it reproduced on a
completely healthy, actively-streaming session. The quit path itself (⌘Q / window-close →
`applicationShouldTerminate` → `kill(SIGTERM, worker_pid)` → `NSTerminateLater` → wait for
the worker's `terminationHandler`) does not appear to be reachable or does not fire as
designed. I did not have log visibility into *why* (worker stdout was `/dev/null` under
`open`, and no crash report or system log entry was found for either hang) — this needs a
rerun via the terminal-redirect launch method to capture whatever (if anything) the app
itself prints, plus investigation of `windowWillClose`/`applicationShouldTerminate`/the
`NSApp terminate:` call chain in `main.mm`.

Both hung instances were cleaned up by the user force-quitting, then confirmed fully gone via
`ps aux` (both `OpenScanner` and `scan_server --live-worker` processes absent) before this
record was written.

**Screen recording** (`~/Desktop/Recording 2026-10-18 at 19.58.47.mp4`, 23.35s, supplied by
the user; reviewed here via `ffmpeg` frame extraction — not staged into the repo, summarized
only): confirms the blank white window throughout, the cursor clicking the red traffic-light
close button with no visible effect on the window (it stays open, content unchanged), and
the window title staying plain **"OpenScanner"** the whole time (this recording covers the
second, healthy-session hang — no crash/session-end title change ever occurred). By the
final frame the user had opened **Force Quit Applications**, and notably **"OpenScanner" is
not in that list at all** — only Claude, Messages, OpenCode, Opera, Safari, Terminal, and
(an unrelated, separately-opened) Xcode appear. This suggests the app may have already
silently detached from the window server / stopped responding to it by that point, rather
than being a normally-running-but-unresponsive process the system would list — consistent
with (not proof of) a deeper hang than a simple blocked main thread.

## Summary

| # | Observation | Result |
|---|---|---|
| 1 | Cold start: page loads (first try or after retry) | **FAIL** — page never loads (404, no retry engages); new bug in `FindUiDir()`'s cwd-relative lookup |
| 2 | Crash surfaced: alert + title, app stays responsive | **PASS** — exact alert text, correct status (9), no hang |
| 3 | Clean exit: no alert, title reads "session ended" | **NOT TESTED** |
| 4 | Graceful quit: ⌘Q → SIGTERM → full teardown chain, no cut | **FAIL, reproduced twice** — quit does not work at all (not a timing issue), on both a crashed and a healthy session; force-quit required both times |

**This run is not a clean pass.** Two of four observations are real, reproducible problems:
the cold-start/UI-serving path is broken when launched as a real `.app` (not just from a
terminal in the build tree), and the quit path does not trigger graceful teardown at all.
Per the project's convention ("the unexpected is the point"), these are reported plainly as
findings, not softened.

## Files
- No raw worker log captured this run (stdout was `/dev/null` under `open`) — see the
  "deviation from the spec" note above.
- Screenshots of the crash alert and quit-hang were viewed directly by the main session (user
  shared them inline in conversation) but are not saved as files in this repo.
