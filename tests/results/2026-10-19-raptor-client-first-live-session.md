# raptor_client — first live session (Phase B / C1 deliverable)

- Date: 2026-10-19
- Worktree: `/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw`
- Branch: `hw/next`
- Target: `build/tools/raptor_client/raptor_client`
- Build freshness: `tools/raptor_client/main.cpp` mtime `Oct 19 00:07:07 2026`;
  binary mtime `Oct 19 00:07:09 2026` — binary is newer than source.
- Pre-run check: no `scan_server`, `smoke_test`, `live_capture_test`,
  `*_message_listener_test`, `OpenScanner`, or `raptor_client` process running
  (`ps aux` filtered, no matches) — confirmed before every step below.
- Raw logs: `tests/results/2026-10-19-raptor-client-first-live-session.log`
  (concatenation of the three steps' stdout/stderr, see below for the same
  content inline).

This tool uses **no vendor SDK** (confirmed by reading `main.cpp`: only
`<arpa/inet.h>`/socket headers plus STL; no OBScan/libOrbbecSDK includes or
linkage). Per the task brief, hard rules 2/3/5/6 (vendor-artifact reading,
`obscan_api.h`, `DYLD_INSERT_LIBRARIES`, launcher-banner check) do not apply
to this binary. Rules 1, 4, 7, 8, 9, 11 do apply and were observed.

Risk callout confirmed by reading `main.cpp` before running `--live`: the
live loop (`Live()`, lines 340-381) calls `SetProperty` for props 3,
152, 153, 154, 155 every iteration (lines 372-376), and the stream-stop call
(`StartStream(rpc, "", 7, ...)`, line 378) only runs after the `while` loop
exits normally. No kill/interrupt was issued against this run.

## Step 1 — `--dry-run`

Command:
```
./build/tools/raptor_client/raptor_client --dry-run
```
Started 2026-10-19 01:02:54, ended 2026-10-19 01:02:55 (~1s). Exit code: **0**.

stdout:
```
[dry-run] start_stream params [6,9,1920,1200,50]               PASS
[dry-run] vendor ReqHeader magic/size/opcode/id                PASS
[dry-run] frame response decodes as bin(3)                     PASS
[dry-run] vendor response magic 0x4252                         PASS
[dry-run] GVCP DISCOVERY_CMD                                   PASS
[dry-run] ok (0 failure(s))
```
stderr: (empty)

**Verdict: PASS.** All 5 framing checks passed, 0 failures, exit 0 as expected.

## Step 2 — `--discover`

Command:
```
./build/tools/raptor_client/raptor_client --discover
```
Started 2026-10-19 01:03:03, ended 2026-10-19 01:03:03 (<1s). Exit code: **0**.

stdout (verbatim):
```
[gvcp] reply 256 bytes: 00 00 00 03 00 f8 00 01 00 01 00 02 00 00 00 01 00 00 30 30 34 30 35 30 80 00 00 07 00 00 00 05 00 00 00 00 00 00 00 00 00 00 00 00 7f 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 4f 72 62 62 65 63 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 43 72 65 61 6c 69 74 79 20 52 61 70 74 6f 72 20 50 72 6f 00 00 00 00 00 00 00 00 00 00 00 0c 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 4f 72 62 62 65 63 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 43 52 30 38 38 36 31 30 30 53 4e 00 00 00 00 00 23 31 30 30 31 30 23 55 53 42 33 2e 30 00 00 00
```
stderr: (empty)

The reply contains the readable ASCII substrings `Orbbec`, `Creality Raptor Pro`,
`Orbbec` (again, second manufacturer field), `CR0886100SN`, `#10010#USB3.0` —
a genuine 256-byte DISCOVERY_ACK from the live daemon, consistent with what
the landing PR's verdict previously observed.

**Verdict: PASS.** Daemon reachable, read-only discovery, exit 0 as expected.

## Step 3 — `--live 127.0.0.1 10013 5` (first-ever live session)

Command:
```
./build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 5
```
Started 2026-10-19 01:03:11, ended 2026-10-19 01:03:16 (~5s, matching the
requested 5-second window — not interrupted, not killed, allowed to run to
its own natural completion).

stdout (verbatim, complete):
```
[live] connected 127.0.0.1:10013
[live] stream started; pulling frames for 5s
[live] pulled 0 frames; stream stopped
```
stderr: (empty)

Exit code: **1**.

Per `main.cpp` line 380 (`return frames > 0 ? 0 : 1;`), exit 1 is the
documented behavior when zero frames were pulled during the window — this is
not a crash and not a hang; the process ran its full 5-second loop and
printed the "stream stopped" line, meaning `StartStream(rpc, "", 7, ...)`
(the stop command) was reached and sent normally. No "saved first frame"
line appeared, because no `ob_rpc_get_frame` call ever returned a
`Value::Kind::Bin` during the 5 iterations-worth of the loop.

**Verdict: the run completed cleanly (no crash, no hang, stop command sent),
but it did not demonstrate live frame capture** — 0 of 0 frames were
non-empty bins. The daemon answered `connect()` and `ob_rpc_start_stream`
(no error was reported from `StartStream`, or `Live()` would have printed
`[live] start_stream failed: ...` and returned 1 before ever printing
"stream started" — it did not), so the stream-start RPC itself was accepted;
whatever happened with frame delivery is "not observed" rather than
"observed to fail" — the tool prints nothing per-iteration about individual
`ob_rpc_get_frame` or `SetProperty` call outcomes (no per-call logging exists
in `Live()`/`SetProperty()`), so there is no evidence in this run's output
about whether the five `SetProperty` calls (props 3, 152-155) were accepted
or rejected by the daemon, nor about why frames were zero. This is a **gap
in observability of this specific build**, not a claim that the daemon
rejected anything.

### Saved frame file

```
$ ls -la /tmp/raptor_frame.raw
ls: /tmp/raptor_frame.raw: No such file or directory
```
Consistent with 0 frames pulled — the file is only opened/written inside the
`if (frame.kind == Value::Kind::Bin)` branch (main.cpp line 362-369), which
never ran.

### Crash report check

```
$ ls -lat ~/Library/Logs/DiagnosticReports/ | head -10
-rw-------@ 1 brucemackay _analyticsusers  7668 Oct 18 21:22 scan_server-2026-10-18-212252.ips
-rw-------@ 1 brucemackay _analyticsusers 14027 Oct 18 20:26 smoke_test-2026-10-18-202650.ips
-rw-------@ 1 brucemackay _analyticsusers  8240 Oct 18 20:11 scan_server-2026-10-18-201119.ips
-rw-------@ 1 brucemackay _analyticsusers 17866 Oct 18 19:10 smoke_test-2026-10-18-191029.ips
-rw-------@ 1 brucemackay _analyticsusers  7174 Oct 18 02:34 probe-2026-10-18-023423.ips
...
```
No `raptor_client-*.ips` entry, and the most recent entry in the directory
(`scan_server-2026-10-18-212252.ips`) predates this run's window
(2026-10-19 01:02-01:03) by several hours. No crash report was found for
this run's time window at the time of this check (2026-10-19, immediately
after step 3). Per rule 11, if one appears later, a dated correction file
will be added rather than editing this record.

## Summary table

| Step | Command | Exit | Check | Result |
|---|---|---|---|---|
| 1 | `--dry-run` | 0 | start_stream params framing | [PASS] |
| 1 | `--dry-run` | 0 | vendor ReqHeader magic/size/opcode/id | [PASS] |
| 1 | `--dry-run` | 0 | frame response bin(3) decode | [PASS] |
| 1 | `--dry-run` | 0 | vendor response magic 0x4252 | [PASS] |
| 1 | `--dry-run` | 0 | GVCP DISCOVERY_CMD bytes | [PASS] |
| 2 | `--discover` | 0 | DISCOVERY_ACK received (256 B) | [PASS] |
| 3 | `--live 127.0.0.1 10013 5` | 1 | connects to daemon | [PASS] |
| 3 | `--live 127.0.0.1 10013 5` | 1 | stream start accepted (no RPC error reported) | [PASS] |
| 3 | `--live 127.0.0.1 10013 5` | 1 | ran full 5s without crash/hang, interrupted by no one | [PASS] |
| 3 | `--live 127.0.0.1 10013 5` | 1 | stop command reached/sent (`stream stopped` printed) | [PASS] |
| 3 | `--live 127.0.0.1 10013 5` | 1 | at least one frame pulled | [FAIL] (0 frames; exit 1 per documented contract, not a crash) |

## Open questions

- Why were 0 frames pulled in this first live session? The tool gives no
  per-call diagnostics (no log line for `ob_rpc_get_frame` returning
  non-bin, nor for the five `SetProperty` calls per iteration), so it is not
  possible from this run's output to say whether: (a) the daemon's frame
  pipe was not actually streaming despite accepting `ob_rpc_start_stream`,
  (b) the `SetProperty` calls for the laser/exposure props were rejected and
  blocked frame delivery, or (c) 5 seconds was too short for this daemon to
  start emitting frames. This is **not observed to fail** at the RPC layer
  (no RPC error strings appeared) — it is an absence of frames, with no
  further diagnostic signal available from this build.
- Should a longer `--live` window, or a build with added per-call logging,
  be tried next? Not attempted here per the "short and deliberate first run"
  instruction and because retries/longer runs weren't authorized by this
  task.
