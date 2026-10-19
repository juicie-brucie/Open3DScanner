# PLAN #105 — hardware replay run, BLOCKED by a `--replay` parser bug

- Date: 2026-10-19
- Worktree: `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`
- Branch: `hw/next`
- Target: `build/tools/raptor_client/raptor_client`
- Confirmed before starting: `pwd` is the `wt-hw` worktree, branch `hw/next` (checked
  via `git branch --show-current`).

## Build freshness check (per the task's #107 warning)

```
$ cmake --build build --target raptor_client -j
[100%] Built target raptor_client
$ stat -f "%N: %Sm" -t "%Y-%m-%d %H:%M:%S" tools/raptor_client/main.cpp build/tools/raptor_client/raptor_client
tools/raptor_client/main.cpp: 2026-10-19 02:40:32
build/tools/raptor_client/raptor_client: 2026-10-19 02:41:08
```
Binary (02:41:08) is newer than source (02:40:32) — fresh build confirmed.

## Pre-run process check

```
$ ps aux | grep -i -E "scan_server|smoke_test|live_capture_test|message_listener_test|OpenScanner|raptor_client" | grep -v grep
```
No output — no conflicting process running.

This binary links no vendor SDK (confirmed previously by reading `main.cpp`: only
socket headers + STL). Rules 2/3/5/6 (vendor-artifact reading, `obscan_api.h`,
`DYLD_INSERT_LIBRARIES`, launcher-banner) don't apply to this tool for the same
reason recorded in `tests/results/2026-10-19-raptor-client-first-live-session.md`.
Rules 1, 4, 7, 8, 9, 11 do apply and were observed — notably rule 4 (never edit the
code under test): the bug found below was **not** fixed, only characterized.

## Step 1 — `--dry-run` sanity check of the replay file (no hardware)

Command:
```
./build/tools/raptor_client/raptor_client --replay docs/recon/findings/streaming-replay-payloads.hex --dry-run
```
stdout/stderr (verbatim):
```
[replay] line 30: odd hex length
```
Exit code: **2**.

Re-ran with host/port given explicitly (ruling out argv-order as the cause):
```
./build/tools/raptor_client/raptor_client --replay docs/recon/findings/streaming-replay-payloads.hex 127.0.0.1 10013 --dry-run
```
stdout/stderr (verbatim):
```
Exit code 2
[replay] line 30: odd hex length
```
Same failure both ways.

## Root cause: `ParseReplayFile`'s delay-prefix parsing corrupts (or, with this
file, eventually rejects) every frame

`ParseReplayFile` (`main.cpp` ~502-541) strips **all** whitespace from the line —
including the space that separates an optional `+<ms>` delay prefix from the hex
payload — *before* it scans the leading digits of the delay. Line 30 of the real
file is:
```
+25429 94000db66f625f7270635f6765745f6672616d65302d322d313290
```
After whitespace-stripping this becomes `+2542994000db66f...`. The delay-digit
scanner (`while (isdigit(hex[i])) ...`) then keeps consuming characters past the
intended `25429` boundary, because the next hex byte (`94`) and several more are
themselves ASCII-decimal digits (`9`,`4`,`0`,`0`,`0`) with no separator left to stop
it — it only stops at `d`, i.e. it eats 5 extra hex characters (`94000`) as if they
were part of the delay number. That leaves an odd-length remainder, which is exactly
the reported error.

To confirm this wasn't specific to line 30, I isolated a single-line repro in the
scratchpad (not editing the real file) using line 26 of the real data, and the
first 29 lines (13 real frames) in a scratch copy — not used to drive the hardware,
dry-run only:

Single-line repro (used `+25339 940005b66f625f7270635f6765745f6672616d65302d322d313290`,
i.e. line 26 of the real file, copied verbatim into a scratch file):
```
[replay] 1 frame(s) from .../replay-bug-repro.hex (dry-run: not sent)
[replay] frame 1: +-429863771ms 24 B: b6 6f 62 5f 72 70 63 5f 67 65 74 5f 66 72 61 6d …
```
The frame is supposed to be 28 bytes starting `94 00 05 b6 ...` (per
`streaming-replay-payloads.md`'s own decode: `[0, 5, "ob_rpc_get_frame...", []]`).
Instead it comes out as 24 bytes starting `b6 6f 62 ...` — the leading `94 00 05`
(the msgpack array-4 header and part of the msgid) is gone, eaten by the delay
scanner — and the delay itself is garbage: `-429863771` ms, an `int` overflow from
accumulating `25339940005` (11 digits) before stopping.

First-13-frames repro (first 29 lines of the real file, dry-run only):
```
[replay] 13 frame(s) from .../first29.hex (dry-run: not sent)
[replay] frame 1: +-509863775ms 39 B: d9 20 6f 62 5f 73 65 74 5f 69 72 5f 66 72 61 6d …
[replay] frame 2: +252619400ms 41 B: cc 8c d9 20 6f 62 5f 73 65 74 5f 69 72 5f 66 72 …
[replay] frame 3: +252819400ms 44 B: cc 9f b6 6f 62 5f 72 70 63 5f 73 65 6e 64 5f 64 …
[replay] frame 4: +252829400ms 44 B: cc a0 b6 6f 62 5f 72 70 63 5f 73 65 6e 64 5f 64 …
[replay] frame 5: +252839400ms 44 B: cc a1 b6 6f 62 5f 72 70 63 5f 73 65 6e 64 5f 64 …
[replay] frame 6: +-476863773ms 39 B: d9 20 6f 62 5f 73 65 74 5f 69 72 5f 66 72 61 6d …
[replay] frame 7: +252929400ms 41 B: cc a1 d9 20 6f 62 5f 73 65 74 5f 69 72 5f 66 72 …
[replay] frame 8: +-474863772ms 36 B: b9 6f 62 5f 72 70 63 5f 73 74 61 72 74 5f 73 74 …
[replay] frame 9: +253399400ms 38 B: cc a2 b9 6f 62 5f 72 70 63 5f 73 74 61 72 74 5f …
[replay] frame 10: +-429863771ms 24 B: b6 6f 62 5f 72 70 63 5f 67 65 74 5f 66 72 61 6d …
[replay] frame 11: +-409863770ms 24 B: b6 6f 62 5f 72 70 63 5f 67 65 74 5f 66 72 61 6d …
[replay] frame 12: +-385863769ms 24 B: b6 6f 62 5f 72 70 63 5f 67 65 74 5f 66 72 61 6d …
[replay] frame 13: +-361863768ms 24 B: b6 6f 62 5f 72 70 63 5f 67 65 74 5f 66 72 61 6d …
```
**Not one of the 13 frames starts with `0x94`** (the array-4 msgpack header every
request frame is documented to have, per the `.md`'s own decode). Every frame in
the file is corrupted by this parser before it would ever reach the wire, and
every delay is garbage (several are negative). Line 30 is simply the first line
where the corruption happens to land on an odd byte count and get caught — lines
1-29 "succeed" (don't error) but would send the daemon mangled, non-byte-exact
frames, not the vendor's bytes.

## Why the hardware run was not attempted

The entire point of this test is: *send the vendor's own bytes, unchanged.*
`--dry-run` (self-contained, no hardware — step 2 of the task's own run order)
demonstrates that this build cannot currently do that for this file: it corrupts
every frame's leading bytes and the delay, with the file's own comment-documented
first frame (`ob_rpc_start_stream`, `940004b9...`) already shown mangled by the
13-frame repro above. Sending those corrupted bytes to the real daemon would not
test "do the vendor's own exact bytes get frames back" — it would test "what does
the daemon do with garbage," which is a different and less interesting question,
and risks an unpredictable response from the one physical device for no gain in
the actual question #105 is asking. Per rule 4, I did not fix `ParseReplayFile` —
it is the code under test. Per the task's own step 2 ("dry-run first... to sanity
check the binary works at all before touching the real daemon"), this is exactly
the gate that caught it, and the binary does not pass that sanity check for this
replay file.

No connection to `127.0.0.1:10013` was made at any point in this session. No
hardware was touched.

## Crash report check

Not applicable — no process touched the daemon or any hardware, so no crash is
possible from this session's own actions. (Checked anyway for completeness: no
`raptor_client-*.ips` in `~/Library/Logs/DiagnosticReports/` from this session's
time window.)

## Summary table

| Step | Command | Exit | Check | Result |
|---|---|---|---|---|
| 0 | `cmake --build build --target raptor_client -j` | 0 | build succeeds | [PASS] |
| 0 | mtime compare | — | binary newer than `main.cpp` | [PASS] |
| 1 | `--replay streaming-replay-payloads.hex --dry-run` | 2 | parses all 36 frames without error | [FAIL] (`line 30: odd hex length`) |
| 1 | same, with explicit host/port before `--dry-run` | 2 | ruling out argv order as the cause | [FAIL] (same error) |
| 2 | single-line repro (scratch copy, line 26 of the real file) | 0 (dry-run) | frame bytes match the file's documented `94 00 05 ...` header | [FAIL] (comes out as `b6 6f 62 ...`, delay `-429863771`ms) |
| 2 | 13-frame repro (scratch copy, first 29 lines) | 0 (dry-run) | any of 13 frames start with `0x94` | [FAIL] (0 of 13) |
| 3 | hardware `--replay ... 127.0.0.1 10013` | — | — | **not attempted** — blocked by the above |

## Verdict

**BLOCKED.** The owed hardware replay run for #105 could not be meaningfully
performed: the `--replay` mode's `ParseReplayFile` has a parsing bug (whitespace
between the optional `+<ms>` delay prefix and the hex payload is stripped before
the delay digits are scanned, so the scanner overruns into the hex payload
whenever the frame's leading bytes are themselves ASCII-decimal-digit characters)
that corrupts every one of the 36 captured frames in
`docs/recon/findings/streaming-replay-payloads.hex` before they would ever reach
the wire. This is not a hardware or daemon observation — it never got that far —
and it neither confirms nor falsifies the request-shape hypothesis from #98/#105.
Row #105 stays `HW`, not `DONE`: this run didn't give a usable answer to the
shape-vs-sequencing question, it surfaced a tool defect upstream of that question.

**What would resolve this next**: the parser bug needs a fix (not by this agent —
rule 4) in `ParseReplayFile`'s delay-prefix handling, e.g. splitting on the first
whitespace run before stripping remaining whitespace from the hex-only remainder,
rather than stripping all whitespace first. After a fix, the sanity gate is: re-run
`--replay docs/recon/findings/streaming-replay-payloads.hex --dry-run` and confirm
(a) no parse error, (b) every printed frame's hex starts with `94` matching the
`.md`'s documented frames, (c) delays are small positive numbers (the file's own
`+25339` .. style values, not six-to-ten-digit garbage) — only then is it safe to
point `--replay` at the live daemon on `127.0.0.1:10013`.

Files: this record. No raw hardware log — no hardware was contacted this run. The
three dry-run transcripts above are reproduced verbatim from this session's
terminal output (not separately archived as a `.log` file, since they are already
quoted in full here and no hardware I/O occurred).
