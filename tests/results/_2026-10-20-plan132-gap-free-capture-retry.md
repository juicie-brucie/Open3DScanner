# PLAN #132 — gap-free capture retry (post-#157 fix) — IR: HUNG on --capture, not retried; depth not attempted

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: `fc6a081`
- Target: build/tools/raptor_client/raptor_client (build-tree copy)
- Preflight: pwd OK; no conflicting process; freshness: binary 2026-10-20 03:54:34 >= newest source main.cpp 03:54:28
- Interposer: n/a — links no vendor SDK
- Run start: 2026-10-20 17:37:24
- Device: Creality Raptor Pro, daemon running, device enumerated, no other SDK user

This is a retry of `2026-10-20-plan132-gap-free-capture.md` (pre-#157,
blocked identically on both ports), following
`2026-10-20-plan127-live-reply-dump-retry.md` earlier in this same
session — in which the **same binary, same IR port (10013), same daemon
session** succeeded cleanly moments before this run (`--dump-replies`
alone: `data=30 empty=3 error=0`, 90.9% data rate).

## Command

```
./build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 60 --frames 200 --capture ~/captures/plan132-ir-retry --capture-seconds 60 --capture-bytes 500000000 --dump-replies
```

(Per #132's own board text: "a separate `--capture` directory per port,
`--frames 200`, `--dump-replies` if #127's data is wanted at the same
time.")

## Result — HUNG, terminated, not retried

- Exit code: `143` (`SIGTERM`, sent by this agent — first and only
  attempt this session, **not a retry**)
- **Observable**: zero output of any kind for over 90 seconds against a
  nominal run that should start producing lines within milliseconds (the
  immediately-preceding plain `--dump-replies` run on the identical port
  printed its first line instantly). `ps` showed the process alive, idle
  (`0:00.00` CPU). The capture directory was created
  (`~/captures/plan132-ir-retry/replies/`) but **empty** — no manifest, no
  reply files, nothing written before termination.
- **This narrows the earlier depth-port hang's explanation**: this hang
  happened on **IR** (port 10013) — the same port, same daemon session,
  same client binary that had just succeeded cleanly with plain
  `--dump-replies` minutes earlier. The only thing that changed is the
  added `--frames 200 --capture <dir> --capture-seconds 60
  --capture-bytes ...` flags. This suggests the hang is more likely tied
  to the `--capture`/`--frames` code path itself (or its interaction with
  a `--live` session already running for a while) than to a specific
  interface/port — though this agent is not asserting that conclusion
  with confidence from two data points, just noting the pattern.
- **Depth capture was not attempted this session** — given two hangs in a
  row today (the earlier depth `--dump-replies` hang, and this IR
  `--capture` hang), this agent held off rather than risk a third hang on
  an already-stressed daemon session, per the coordinator's guidance to
  continue only with what's genuinely hands-free/low-risk.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-20-plan132-gap-free-capture-retry '2026-10-20 17:37:24' raptor_client`:

> hw-postflight: no crash report for 'raptor_client' at/after 2026-10-20 17:37:24 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## Open questions

- Is the hang specific to `--capture`/`--frames` (as this run's pattern
  suggests), or to the daemon/session state after several prior
  connections in the same session? Not determined — only two data points
  exist (this run and the earlier depth hang), both on the same daemon
  session, so session-accumulated state can't be ruled out either.
- Would a fresh daemon session (not one that's already handled several
  prior `raptor_client` connections today) behave differently? Not
  tested.

## Board

- Row move I would propose for **#132**: stays open/unresolved — the
  gap-free capture itself could not be obtained for either interface this
  session. #127's plain dump (no `--capture`) did succeed for IR, so the
  underlying stream/data path works; something specific to the capture
  path is the suspect. Recommend a dedicated, isolated follow-up (fresh
  daemon session, `--capture` only, no `--dump-replies` combined, to
  narrow the variable) before the next attempt.
