# PLAN #105 — hardware replay run, BLOCKED by a mid-replay hang

- Date: 2026-10-19
- Worktree: `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`
- Branch: `hw/next` (confirmed via `git branch --show-current` before starting)
- Target: `build/tools/raptor_client/raptor_client`
- Raw log: `tests/results/2026-10-19-plan105-replay-hang-blocked.log` (copy of
  `/tmp/plan105-replay-live.log`, captured verbatim below)

## Preconditions checked

- Parser fix confirmed present: `git log` shows `8816290` ("fix-forward PR #267:
  split delay prefix before stripping whitespace") in this branch's history.
- Build freshness: `cmake --build build --target raptor_client -j` → `Built
  target raptor_client` (no-op, already current).
  `stat` comparison: `tools/raptor_client/main.cpp` mtime `2026-10-19 14:25:50`,
  `build/tools/raptor_client/raptor_client` mtime `2026-10-19 14:25:52` — binary
  newer than source. Fresh build confirmed.
- Process check (name-anchored, rule 13): `pgrep -x raptor_client` → none before
  starting. `pgrep -x scan_server` found pid 56611, `ps aux` showed it as
  `build/tools/scan_server/scan_server 8791` launched from the **main**
  `/Users/brucemackay/DevReposiMac/Open3DScanner` checkout (not this worktree),
  bound to port 8791 (an unrelated HTTP smoke check, not GVCP/RPC, not port
  10013). Judged not a conflict with the port-10013 daemon this test targets.
- This binary links no vendor SDK (per the prior `2026-10-19-raptor-client-first-live-session.md`
  and `2026-10-19-plan105-replay-parser-bug-blocked.md` findings) — rules
  2/3/5/6 (vendor dylibs, `obscan_api.h`, `DYLD_INSERT_LIBRARIES`, launcher
  banner) don't apply to this tool.

## Step 1 — dry-run sanity gate (no hardware)

```
$ ./build/tools/raptor_client/raptor_client --replay docs/recon/findings/streaming-replay-payloads.hex --dry-run
```
Exit code: **0**. Output: 36 frames printed, every one's hex starts with `94`
(the documented msgpack array-4 header), delays all small positive values
(`+25259ms` .. `+25515ms`, matching the file's own `+NNNNN` style — no
multi-hundred-million-ms garbage, no negatives). This matches exactly the
sanity gate the prior blocked record (`2026-10-19-plan105-replay-parser-bug-blocked.md`)
specified as the condition for proceeding to hardware. **The parser fix holds.**

## Step 2 — hardware replay

Command:
```
./build/tools/raptor_client/raptor_client --replay docs/recon/findings/streaming-replay-payloads.hex 127.0.0.1 10013
```

Verbatim stdout/stderr (log file, in full):
```
[replay] 36 frame(s) from docs/recon/findings/streaming-replay-payloads.hex
[replay] connected 127.0.0.1:10013; sending unchanged request frames
[replay] frame 1 -> kind=7
[replay] frame 2 -> kind=7
[replay] frame 3 -> kind=7
[replay] frame 4 -> kind=7
[replay] frame 5 -> kind=7
EXIT: 143
```
(`EXIT: 143` = `128 + 15`, i.e. the process's own exit status after the
`SIGTERM` I sent to end the hang — not a status the program printed on its
own, appended by my wrapper script.)

Timeline: process started ~14:36:14 EDT. It connected and sent 5 frames almost
immediately. After `frame 5 -> kind=7` it produced **no further output** —
no `frame 6`, no error, no completion message — for **over two minutes**
(wall-clock confirmed: the shell's own foreground invocation was moved to
background by the harness at the 120 s mark with zero visible progress past
frame 5; I rechecked at 14:38:24, i.e. ~2m10s after start, and the process
(`pid 57221`) was still alive at 0.0% CPU, suggesting a blocked read rather
than a spin). Per rule 8, **I did not retry or re-send anything** — I sent
`kill 57221` (`SIGTERM`) to end the hung client process only (not the device,
not a power cycle — rule 9), confirmed it exited, and stopped.

## Why this is reported as a hang, not a pass or a clean failure

The replay neither completed (36/36 frames) nor failed cleanly with an error
message — it connected, sent 5 of 36 frames, then stopped producing any
observable progress. This is exactly the hang pattern rule 8 warns about. I
terminated only my own client process; I did not touch, unplug, or power-cycle
the device, and I did not retry the replay.

## Crash report check

`~/Library/Logs/DiagnosticReports/` checked for the run's window
(~14:36–14:39 EDT): no `raptor_client-*.ips` present. Expected — the process
was ended by my own `SIGTERM`, not a crash.

## Summary table

| Step | Command | Exit | Check | Result |
|---|---|---|---|---|
| 0 | `cmake --build build --target raptor_client -j` | 0 | fresh build | [PASS] |
| 0 | mtime compare | — | binary newer than `main.cpp` | [PASS] |
| 0 | `pgrep -x raptor_client` / `pgrep -x scan_server` | — | no conflicting process on this target | [PASS] (one unrelated `scan_server` on port 8791 in a different checkout, judged non-conflicting) |
| 1 | `--replay streaming-replay-payloads.hex --dry-run` | 0 | all 36 frames parse, `0x94`-prefixed, small positive delays | [PASS] |
| 2 | `--replay streaming-replay-payloads.hex 127.0.0.1 10013` | 143 (after my `SIGTERM`) | connects to daemon | [PASS] |
| 2 | same | 143 | sends unchanged request frames | [PARTIAL] — 5 of 36 sent (`kind=7` each), then no further output |
| 2 | same | 143 | completes 36/36 without hang | [FAIL] — hung after frame 5, did not self-recover in >2 minutes, ended by operator `SIGTERM` |

## Verdict

**BLOCKED** (hang, not a pass). The parser fix (`8816290`) is confirmed correct
by the dry-run sanity gate — the owed hardware run no longer fails on parsing.
But the live replay against the real daemon hung after 5 of 36 frames with no
error and no progress for over two minutes. Per rule 8, this was not retried.
Row #105 stays off `DONE`. This is a new, different blocker from the prior
parser bug — the next owed step is diagnosing why the daemon or client stalls
after frame 5 (candidates: a reply the client is blocking on that never
arrives, a request shape frame 6 sends that the daemon doesn't like, or a
cadence/timing issue — none of this run's output distinguishes among them).

**Open question**: is the daemon on `127.0.0.1:10013` now in a state affected
by receiving 5 of 36 frames and then having the client disappear mid-sequence
(no explicit stop/close sent)? This run did not check the daemon's
post-hang state — doing so was judged outside this run's scope once the hang
was identified, per "report it and stop."
