# PLAN #105/#120/#122 — live replay rerun with per-interface routing + envelope unwrap — bin=0 still, diagnostic only

- Date: 2026-10-19
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next, HEAD `9c54deb` ("Merge pull request #312 from juicie-brucie/build/plan122-envelope-unwrap")
- Target: build/tools/raptor_client/raptor_client (build-tree copy (fixed order: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; freshness: binary 2026-10-19 17:41:12 >= newest source main.cpp 17:41:09; `git diff HEAD -- tools/raptor_client/` empty (working tree matches HEAD, no rebuild needed)
- Interposer: n/a — `raptor_client` links no vendor SDK (socket/STL only); vendor-artifact & interposer rules do not apply
- Preconditions confirmed: #120 landed at `5ea8684` ("builder: #120 replay interface routing"), #122 landed at `9cd201c` ("builder: #122 envelope unwrap"), both present in `git log` for `tools/raptor_client/` on this HEAD
- Run start (dry-run / preflight): 2026-10-19 17:45:56
- Run start (live): 2026-10-19 17:46:21
- Device: Creality Raptor Pro, USB3.0 — user confirmed attached, no other SDK user, one run only

## Commands

```
./build/tools/raptor_client/raptor_client --replay docs/recon/findings/streaming-replay-payloads.hex --dry-run
./build/tools/raptor_client/raptor_client --replay docs/recon/findings/streaming-replay-payloads.hex 127.0.0.1 --reply-timeout-ms 2000
```

**Note on the live command's arguments:** the task's draft command included a
`10013` port positional after the host, matching the pre-#120 single-port
usage. With #120 landed, the tool's own argument parser now rejects a second
positional explicitly: `"the replay now routes per interface; a single port
argument is ambiguous — use --ir-port and --depth-port"` (exit 2, confirmed
by reading `tools/raptor_client/main.cpp:1467-1470` before running anything).
The updated `--replay` usage (`main.cpp:1437`) is `--replay <file> [host]
[--dry-run] [--quiet] [--reply-timeout-ms N] [--ir-port N] [--depth-port N]`
— it self-routes internally (IR default 10013, depth default 10010) and
takes only one optional positional (`host`). The command above uses that
corrected form; neither port override flag was needed since the daemon's
ports match the defaults.

Neither command was retried; the live command was not killed early.

## Result — dry-run

- Exit code: `0`
- All 36 frames parsed.
- Cadence line (pasted verbatim):
  ```
  [replay] cadence: 36 frame(s), capture span 256 ms, mean gap 10.74 ms, first-frame wait 250 ms (backward stamps clamped to 0)
  [replay] interfaces: IR 13 (span 256 ms) / depth 23 (span 211 ms)
  ```
  The per-interface breakdown line is new since the prior rerun
  (`2026-10-19-plan105-replay-rerun.md`), confirming #120's dry-run output
  change landed as described.

## Result — live run

- Exit code: `1`
- Raw log: `2026-10-19-plan120-122-replay-rerun.raw.log` (same directory; both commands' output, dry-run then live)
- Connection lines: `[replay] connected 127.0.0.1:10013 for interface IR (0-2-12)` and `[replay] connected 127.0.0.1:10010 for interface depth (0-2-10)` — confirms both ports were opened, per #120.
- Per-frame replies (verbatim, `kind=N` per `tools/raptor_client/main.cpp:108`'s enum — `Nil=0, Bool=1, Int=2, UInt=3, Double=4, Str=5, Bin=6, Array=7, Map=8, Error=9, Timeout=10`):
  - Frames 1,2,6,7,8,9: `kind=1` (Bool)
  - Frames 3,4,5,30-36: `kind=5` (Str)
  - Frames 10-29: `kind=7` (Array)
  - No frame returned `kind=6` (Bin).
  - No `error:` line, no `no reply within 2000 ms` line, for any frame.
- Final line: `[replay] done: bin=0 empty=36 error=0`

## Outcome bucket

**Still `bin=0 empty=36`**, per the task's own interpretation table — "the
envelope fix isn't producing content." Both #120 (dual-port routing, two
live connections confirmed, correct ports per interface) and #122 (per the
dry-run's new interface-count line, at minimum the build the task expects
is running) are observably active in this binary, and the reply *kind
distribution* changed from the prior rerun (all `kind=7` before; now a mix
of `kind=1`/`kind=5`/`kind=7`) — so something about the reply classification
or the request/response exchange did change between the two runs. But the
bottom-line count is unchanged: zero `kind=6` (Bin) replies across all 36
frames, in either run.

Per the coordinator's own instruction for this bucket ("investigate further,
report back before concluding") and per my role's limits, I am reporting
this observable and stopping here rather than tracing why the kind
distribution shifted or why no reply is `Bin` — that diagnosis belongs to
the builder/recon room, not this record.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-19-plan120-122-replay-rerun '2026-10-19 17:45:56' raptor_client`:

> hw-postflight: no crash report for 'raptor_client' at/after 2026-10-19 17:45:56 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

Expected and unremarkable — `raptor_client` links no vendor SDK.

## On failure or block — observable + one lead

- Observable: `./build/tools/raptor_client/raptor_client --replay docs/recon/findings/streaming-replay-payloads.hex 127.0.0.1 --reply-timeout-ms 2000` → exit 1, `[replay] done: bin=0 empty=36 error=0`; replies are `kind=1`/`kind=5`/`kind=7` across the 36 frames, never `kind=6` (Bin), despite #122's envelope-unwrap landing and #120's per-interface routing both being observably active (dual connections, per-interface dry-run counts).
- Lead: likely in how the unwrapped envelope's result value is classified/extracted for these specific reply shapes (`tools/raptor_client/main.cpp`'s envelope-unwrap path added by #122) — the reply kinds seen (`Bool`/`Str`/`Array`) suggest the daemon's actual responses to these request bytes are not literally `[1, msgid, nil, <bin>]`-shaped for most of these calls, so #122's unwrap may be looking in the wrong place or the daemon's replies to these specific vendor-captured bytes genuinely aren't binary for most of the 36.

## Open questions

- Is any `kind=1`/`kind=5` reply expected for some of these 36 calls (e.g.
  an ack/status reply for a `set_*`/`start_*` call rather than a frame-pull
  call), making the *all-`kind=7`* result on the earlier rerun the anomaly
  and this run's mix the more correct behavior — just still missing the
  `Bin` frame-data replies specifically? Or should every one of the 36
  calls in this capture return binary frame data? Not settled here.
- Given the kind distribution changed between the two reruns using the same
  capture file, same daemon, same device — is that attributable to #120's
  routing changing which calls go to which port (so the daemon answers
  differently depending on connection/port), to #122's unwrap changing how
  the same raw bytes are interpreted, or to both? Not settled here.

## Board

- Row move I would propose for **#120**: `HW` → `DONE`. Acceptance was
  "completes 36/36, or names the unanswered frame AND its interface" — this
  run completed 36/36 (every frame got a reply on its routed port, no
  timeouts), which is the acceptance condition as stated, independent of
  frame *content*.
- Row move I would propose for **#122**: stays `HW`, not `DONE`. Acceptance
  was "replay reports bin=36 against a mock, and the live GET_FRAME lines
  show bin(N)" — this run reports `bin=0`, so the stated acceptance (content
  arriving) is not met on hardware, even though the unwrap code is
  observably running (reply kind distribution changed from the prior run).
- Row move I would propose for **#105**: stays `HW`, not `DONE`. Still no
  binary frame content from the daemon for the vendor's own replayed
  request bytes, now with both #120 and #122 ruled out as the blocking
  mechanism (routing is correct per-interface; the envelope-unwrap path is
  active). The remaining gap has narrowed to: why does the daemon's actual
  reply to these specific request bytes not carry binary frame data,
  regardless of unwrap logic — a question for recon/builder to take from
  the reply-kind evidence in this record, not something I diagnosed further.
