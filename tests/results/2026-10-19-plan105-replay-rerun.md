# PLAN #105 — live replay diagnostic (post-#117 baseline for #120) — diagnostic run, not #105's acceptance

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next (fast-forwarded to `9d228f7` immediately before this run; no local commits ahead, only untracked `tests/results/` files that were superseded by already-landed identical copies on `main` and removed before merging)
- Target: build/tools/raptor_client/raptor_client (build-tree copy (fixed order: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; freshness: binary 2026-10-19 16:23:58 >= newest source main.cpp 16:23:56 (source unchanged since `4fe383c` "builder: #117 replay pacing + bounded reply read", confirmed via `git diff HEAD -- tools/raptor_client/` = empty; no rebuild needed)
- Interposer: n/a — `raptor_client` links no vendor SDK (socket/STL only); vendor-artifact & interposer rules do not apply
- Run start (dry-run): 2026-10-19 16:55:30 (preflight's recorded start)
- Run start (live): 2026-10-19 16:55:43
- Device: Creality Raptor Pro, USB3.0 — user confirmed attached, no other SDK user, one run only

## Commands

```
./build/tools/raptor_client/raptor_client --replay docs/recon/findings/streaming-replay-payloads.hex --dry-run
./build/tools/raptor_client/raptor_client --replay docs/recon/findings/streaming-replay-payloads.hex 127.0.0.1 10013 --reply-timeout-ms 2000
```

Neither command was retried; the live command was not killed early.

## Result — dry-run

- Exit code: `0`
- All 36 frames parsed.
- Cadence line (pasted verbatim):
  ```
  [replay] cadence: 36 frame(s), capture span 256 ms, mean gap 10.74 ms, first-frame wait 250 ms (backward stamps clamped to 0)
  ```

## Result — live run

- Exit code: `1`
- Raw log: `2026-10-19-plan105-replay-rerun.raw.log` (same directory; both commands' output, dry-run then live)
- Verbatim tail:
  ```
  [replay] connected 127.0.0.1:10013; sending unchanged request frames (reply timeout 2000 ms)
  [replay] frame 1 -> kind=7
  ...
  [replay] frame 36 -> kind=7
  [replay] done: bin=0 empty=36 error=0
  ```
  All 36 frames printed `-> kind=7` (the `Kind::Array` value, per the enum at
  `tools/raptor_client/main.cpp:108`); no `error:` line, no `no reply within
  2000 ms` line for any frame.
- **Outcome bucket, per the task's own interpretation table: `bin=0 empty=36`
  — the daemon answered every one of the 36 frames (no timeouts, no
  disconnects), but none of the 36 replies carried a binary frame.** Request
  shapes were accepted by the daemon; no frame content was produced.
- **Note on exit code vs. the task's guess:** the task description expected
  "exit 0" for this bucket; the actual exit code was `1`. Reading
  `tools/raptor_client/main.cpp:870` (`return bins > 0 ? 0 : 1;`) shows the
  tool's own convention returns non-zero whenever zero binary frames were
  received — exit 1 here is consistent with `bin=0`, not a contradiction of
  the bucket, just a different exit-code convention than guessed.
- The interface-suffix lookup step (depth vs. IR, `302d322d3130` /
  `302d322d3132`) does not apply to this run — it was specified only for the
  "no reply within N ms" bucket, and no frame timed out here.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-19-plan105-replay-rerun '2026-10-19 16:55:30' raptor_client`:

> hw-postflight: no crash report for 'raptor_client' at/after 2026-10-19 16:55:30 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

Expected and unremarkable — `raptor_client` links no vendor SDK, so no
device-process crash report could originate from it regardless of outcome.

## Payload mix (context, not a variable tested here)

13 IR frames (`0-2-12`) and 23 depth frames (`0-2-10`), all sent to port
10013 in this build. Since every frame got a reply (just not a binary one),
this run does not distinguish IR-suffixed from depth-suffixed handling —
that distinction (PLAN #120) remains untested by this run.

## Open questions

- The daemon accepted and replied to all 36 of the vendor's own
  byte-exact request frames, but returned zero binary payloads (`kind=7`/
  `Array` for every reply) — is `kind=7` the daemon's normal "accepted,
  streaming not yet active" reply, or does it indicate the request sequence
  is missing a step (e.g. a separate `start_stream`/property-set call this
  replay doesn't send) that the vendor's own client performs before frames
  flow? Not settled here — recon's/builder's question.
- Given every frame got a reply with no timeout, is PLAN #120's per-interface-
  routing concern (whether a depth-suffixed frame specifically goes
  unanswered) still live, or does this run's "every frame answered" result
  suggest the routing question is moot on this daemon/build? Not settled
  here — this run simply didn't produce the failure mode #120 anticipates.

## Board

- Row move I would propose for **#105**: **stays `HW`**, not `DONE`. This
  run supplies the post-#117 baseline the task asked for — it rules out the
  pacing/unbounded-read bug as the cause of "zero frames" (every frame now
  gets a timely reply) — but #105's own acceptance criterion (the `--live`
  path actually producing frames) is still unmet: the daemon answers but
  produces no binary content for the vendor's own replayed request bytes.
  That keeps the gap on the daemon-response-content side, not the client's
  pacing/timeout mechanics, which is useful new information for #105/#120
  but not a pass on #105 itself. The dispatcher may want to log this
  specifically as "#120's baseline: no per-frame timeout observed; the
  open question is the `kind=7`/empty-reply content, not interface
  routing" when deciding #120's next step.
