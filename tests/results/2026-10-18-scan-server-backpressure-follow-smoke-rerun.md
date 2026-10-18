# 2026-10-18 — scan_server `--live` backpressure (#41) + follow-pose (#48) smoke, RERUN (crashed before completion)

## Context

Rerun of the combined #41/#48/#32/#43/#46 smoke test, redone because the
earlier same-day attempt (`tests/results/2026-10-18-scan-server-backpressure-follow-smoke.md`)
hit row #54 (`ui/app.js` duplicate `const frame` SyntaxError killing the
page). That fix landed as PR #138 (`b46662b`, confirmed present in this
worktree's `git log` before this run). This run was concurrent with the main
session driving a real browser tab against the live UI on port 8788 as a
WebSocket client, toggling the follow-camera button, and briefly blocking the
browser tab's JS thread to exercise the client-side drop-gate.

## Target / command

```
cd /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
./build/tools/scan_server/scan_server --live 8787 "" 300
```

Pre-run checks:
- `pwd` confirmed `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`, branch `hw/next`.
- No other SDK-using process running (`ps aux` checked for `scan_server`,
  `device_message_listener_test`, `data_process_message_listener_test`,
  `live_capture_test`, `smoke_test` — no matches, run from the correct
  worktree so the guard hook didn't block the check itself).
- Build freshness: `build/tools/scan_server/scan_server` mtime `Oct 18 05:07`,
  `tools/scan_server/main.cpp` mtime `Oct 18 04:29` — binary newer than
  source, not rebuilt by me.
- PR #138 (`b46662b`, "hotfix: remove duplicate const frame binding in
  app.js onmessage") confirmed present in `git log --oneline -- ui/app.js`
  in this worktree.
- Run start: ~05:09:35 EDT 2026 (pre-run `date` check); process launched
  ~05:09:43 per the first SDK-internal timestamp in the log.

## Outcome: the run crashed before completion — it is NOT a repeat of the prior clean PASS

**Exit code: 2**

Last line of output:
```
[launcher] Worker crashed or timed out inside the vendor SDK (child killed by signal Broken pipe: 13).
```

This is **not** the "exit code 2 = no SDK dylib / skipped" case from rule
10 — the SDK plainly loaded and ran for ~500 ticks with a real device
attached (`CR-Scan Raptor Pro`, SN `CR0886100SN` enumerated and connected).
This is the launcher's own crash/timeout exit path reporting that the child
worker process was killed by **signal 13 (SIGPIPE / "Broken pipe")**
partway through the session. The log shows no watchdog "timed out" message
and no 600s elapsed (the whole run lasted under 2 minutes), so despite the
launcher's combined "crashed or timed out" wording, the signal number and
short elapsed time point to a **crash (SIGPIPE), not a timeout**. Per rule
8 (never retry a timeout) and the general instruction not to retry a failed
run without direction, **I did not retry.**

## Checks

| # | Check | Result |
|---|---|---|
| 1 | Banner / launcher-correctness (rule 6) | **[substitute present, literal line absent — same as every prior run]** `[vulkan_disable_interpose] loaded` appears as log line 2, right after the one `[launcher] Running live scan server...` line. The literal `DYLD_INSERT_LIBRARIES=...` line has never appeared in this binary's output across multiple runs — not a new finding, per task framing. Treating the substitute as sufficient confirmation this was the interposed run. |
| 2 | #41 tick cadence steady, within the observed window | **[PASS, for the portion that ran]** Tick lines present every 50 ticks from tick 0 through tick 500 (`0, 50, 100, 150, 200, 250, 300, 350, 400, 450, 500`), no gaps, no stall, `clients=2` consistently from tick 50 onward (vs. the prior run's `clients=1` — consistent with the main session's browser tab plus whatever else counts as a second client this time). The run crashed immediately after the tick-500 line, so **cadence beyond tick 500 was never observed** — this is "not observed," not "confirmed steady for the full session." |
| 3 | #41 client-count drop/prune signal | **[NOT OBSERVED, same as before]** `clients=` stayed at `2` for the whole observed window; no evidence of the slow-client send-timeout/drop path firing, nor any indication it caused the subsequent crash from the log alone. |
| 4 | **Possible #41-adjacent new finding: the crash signal itself** | **[FLAGGED, not confirmed]** The process died to `SIGPIPE` within ~90s of start, with `clients=2` the whole time and the main session reportedly about to block its browser tab's JS thread to exercise the drop-gate. SIGPIPE is the classic symptom of a `write()`/`send()` to a socket whose peer has already closed/reset, when the writer hasn't suppressed the default signal disposition (e.g. no `SO_NOSIGPIPE`/`MSG_NOSIGNAL`). I cannot confirm from this log alone that the main session's stall-the-JS-thread action is what triggered this (I have no timestamp correlation with the main session's actions), but the timing (crash during exactly the window the client-side exercise was meant to happen) and the signal's nature are suspicious enough to report plainly rather than wave off. This should be cross-checked against the main session's own timeline. |
| 5 | #48 pose-broadcast call ran every un-paused tick, within the observed window | **[PASS, for the portion that ran]** All six tick lines observed (0 through 500) show `paused=0`; the `encode_pose`/broadcast call site in the tick loop is unconditional except for the `hardware_paused` gate, so structurally it ran every tick up to the crash. No per-tick byte-level confirmation available (no such log line exists in this binary, consistent with source inspection noted in the prior run's record). Cannot say anything about ticks past 500 — never reached. |
| 6 | #32 point-count ramp ≥1,000,000 | **[FAIL to reach threshold — not a contradiction of #32, just an incomplete run]** Point count only reached `52,398` (tick 500, max seen so far also `52,398`) before the crash — nowhere near the ≥1,000,000 threshold the prior run sustained. This run simply didn't last long enough to accumulate points; it is **not** evidence against #32 (the prior run already demonstrated the threshold is reachable and sustainable: `[hw-confirmed: 2026-10-18-scan-server-backpressure-follow-smoke]` reached 1,044,043+ by tick 300 and sustained well above 1M through teardown). This run neither confirms nor contradicts #32 — it's simply incomplete. |
| 7 | #43/#46 teardown — all five observations | **[NOT OBSERVED — none of it ran]** No `obscan_scan_stop`, `obscan_session_destroy`, or `obscan_context_destroy` lines anywhere in the log (`grep` confirms zero matches). The crash happened before any teardown call was reached; the launcher's crash-exit path does not appear to run the normal pause/stop/destroy sequence at all. Per the prior run's record, #43/#46 were already fully confirmed ([hw-confirmed: 2026-10-18-scan-server-backpressure-follow-smoke]) — this run contributes nothing new either way for teardown, since teardown never started. |
| 8 | Crash report (rule 11) | **[checked, none found]** `~/Library/Logs/DiagnosticReports/` was checked after the run (`ls -lat`); the most recent entry is an unrelated `probe-2026-10-18-023423.ips` from 02:34, over 6 hours before this run. No `scan_server-*.ips` entry exists for this run's time window (~05:09–05:11). This is consistent with `SIGPIPE`'s default disposition on macOS being a plain process termination (not a Mach exception), which typically does **not** generate a `.ips` crash report — so the absence of a report here is expected for this failure mode, not evidence the crash didn't happen (the launcher's own stdout line is direct evidence it did). |

## Verdict: **BLOCKED / INCOMPLETE** — the run crashed (SIGPIPE, exit code 2) before completing the session; #32 and #43/#46 were not re-confirmed by this run (they remain confirmed only by the earlier clean run); #41/#48 server-side evidence is partial (sound through tick 500, unknown after)

## Raw log

`tests/results/2026-10-18-scan-server-backpressure-follow-smoke-rerun.log` — copied verbatim from the command's captured stdout/stderr, including the final `EXIT_CODE=2` line appended by my capture wrapper (not part of the program's own output).

## Open questions for the main session

1. What was the main session doing at the wall-clock moment of the crash?
   The worker's last tick line (500/3000) corresponds to SDK-internal
   timestamps around `05:10:4x` EDT per the raw log's interleaved
   timestamped SDK lines; the crash followed within seconds. If the
   browser-tab JS-thread stall was happening around then, that's a strong
   candidate cause for the `SIGPIPE` and should be treated as a real
   finding (likely a missing `SO_NOSIGPIPE`/`MSG_NOSIGNAL` on the server's
   send path), not dismissed.
2. Should this be retried? I did not retry on my own authority — this
   wasn't a hang (rule 8 is about hangs specifically, and the process did
   exit, not hang), but I'm not re-running without direction given "one run
   at a time" and the need to understand the cause first. The device was
   not power-cycled or unplugged.
