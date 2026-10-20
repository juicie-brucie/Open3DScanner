# PLAN #127 — raptor_client --live --dump-replies retry (post-#157 fix) — IR: PASS with real frame data; depth: HUNG, terminated

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: `fc6a081`
- Target: build/tools/raptor_client/raptor_client (build-tree copy)
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 2026-10-20 03:54:34 >= newest source main.cpp 03:54:28
- Interposer: n/a — links no vendor SDK (socket/STL only)
- Run start: 2026-10-20 17:34:50
- Device: Creality Raptor Pro, CrealityScan daemon running, device enumerated, no other SDK user

This is a retry of the earlier `2026-10-20-plan127-live-reply-dump.md`
(BLOCKED on `ob_rpc_start_stream` argument-count mismatch), now that
#157's interface-suffix fix is confirmed landed in the real live path
(verified in source during this session's `ungated-smoke-batch3`
investigation, and independently corroborated by #157's own PLAN.md text:
"Landed... The daemon check is owed").

## Commands

```
./build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 5 --dump-replies
./build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 5 --dump-replies
```

## Result — IR (port 10013): PASS, the #157 fix resolves #105's original question

- Exit code: `0`
- The daemon's `start_stream`/`get_frame`/`send_data`/`stop_stream` calls
  all succeed with the suffixed method names (`ob_rpc_start_stream0-2-12`,
  `ob_rpc_get_frame0-2-12`, etc.) — confirms #157's fix works against the
  real daemon, not just the mock selftest.
- **`[live] done: get_frame=33 data=30 empty=3 error=0; stream stopped`**
  — **30 of 33 `get_frame` calls returned real binary frame data** (each
  a `Bin(2304000)`-byte payload — a full IR image), only 3 were empty.
  `[dump] tally: port 10013 data=30 empty=3 other=0 (data share 90.9%)`.
- This is a dramatic change from every other run referencing this
  pipeline today (and from #105's original finding) — the daemon is now
  genuinely serving real frame content on the IR interface once the
  request uses the correct suffixed method names.
- Sample `get_frame` reply (msgid=188): `result=array(11)
  [0]=UInt(0) [1]=UInt(1792532106406514) [2]=UInt(179057955) [3]=UInt(255)
  [4]=Bin(2304000) head=[10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10
  10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 0f ...] [5]=UInt(2304000)
  [6]=Bin(0) head=[] [7]=UInt(0) [8]=Bin(6) head=[a2 1b 00 00 55 00]
  [9]=UInt(6) [10]=UInt(1)`
- Raw log (full, all 33 `get_frame` calls + every `send_data`/property
  set): `2026-10-20-plan127-live-reply-dump-retry-ir.raw.log`

## Result — depth (port 10010): HUNG, terminated (not retried)

- Exit code: `143` (`SIGTERM`, sent by this agent to end the hang —
  **not a retry**, this is the first and only attempt on this port this
  session)
- **Observable**: the process started, connected (per the command
  succeeding at the OS level — the process existed and was running), but
  **produced zero output of any kind** — not even the initial `[live]
  connected...` line that prints immediately on every other successful
  run today — for over 65 seconds against a nominal ~5s run. `ps` showed
  the process alive but at `0:00.00` CPU time (blocked, not spinning).
  Raw log file is empty (0 bytes):
  `2026-10-20-plan127-live-reply-dump-retry-depth.raw.log`.
- Per role (never retry a timeout/hang), this agent terminated the
  process (`kill -TERM`) after confirming it was genuinely stalled, not
  slow, and recorded the observation rather than retrying.
- Lead: likely something depth-interface-specific in the connect/
  start_stream sequence on port 10010 stalls before any output is even
  flushed — given IR (10013) on the identical binary/daemon/session
  succeeded cleanly moments earlier, this is specific to the depth
  interface's path, not a general client or daemon failure. Not
  diagnosed further here.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-20-plan127-live-reply-dump-retry '2026-10-20 17:34:50' raptor_client`:

> hw-postflight: no crash report for 'raptor_client' at/after 2026-10-20 17:34:50 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

(No crash expected or found for either outcome — the depth hang is a
stall, not a crash.)

## Open questions

- Why does the depth interface (port 10010) hang with zero output while
  the IR interface (port 10013), using the identical client binary and
  the same session, succeeds cleanly? Not investigated — flagged as the
  next question for #132 or a dedicated depth-focused retry.
- Is the depth hang reproducible, or was this a one-off? Not re-tested
  here per the no-retry rule — a future attempt should treat this as a
  known risk and have a bounded-wait/kill plan ready rather than an open
  wait.

## Board

- Row move I would propose for **#127**: `HW` → `DONE` **for the IR
  interface** — the owed live run is complete, the array's live contents
  are identified (a real `Bin(2304000)` frame band in slot `[4]`, matching
  the structure #122's hardware rerun already established), and #105's
  original "zero frames" question is answered for IR: it's not zero
  anymore post-#157. **Depth stays open** — the hang needs its own
  follow-up before depth can be marked done for this row.
- This also bears on **#132**: IR's "gap-free capture" should now be
  straightforward given this run's clean 90.9% data rate; depth's capture
  will hit the same hang found here and should not be attempted again
  without addressing it first.
