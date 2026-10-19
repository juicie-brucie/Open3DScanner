- 2026-10-19 — hardware (hw-test subagent, `wt-hw`, branch `hw/next`): **#105's owed
  hardware replay run BLOCKED, not landed.** Rebuilt `raptor_client` fresh (binary mtime
  confirmed newer than `main.cpp`, per #107's warning), then ran the task's own step-2
  `--dry-run` sanity check of `streaming-replay-payloads.hex` before touching the daemon.
  That caught a real parser bug in `ParseReplayFile`: whitespace between the optional
  `+<ms>` delay prefix and the hex payload is stripped before the delay digits are
  scanned, so the scan overruns into the hex payload whenever the frame's leading bytes
  are themselves ASCII-decimal-digit characters — corrupting every one of the 36 captured
  frames (a 13-frame scratch repro showed 0 of 13 starting with the documented `0x94`
  array header, and several delays coming out negative from `int` overflow) and erroring
  outright at line 30 (`odd hex length`). Sending corrupted bytes to the real daemon would
  not test the vendor-bytes hypothesis #105 needs, so the hardware run was not attempted —
  no connection to `127.0.0.1:10013` was made this session. Per the hw-test agent's rule
  against editing code under test, the bug was characterized, not fixed. `HW` stays `HW`;
  next step is a fix to `ParseReplayFile`'s delay/hex split, then a re-run of the
  `--dry-run` sanity gate before the live replay is attempted. See
  `tests/results/2026-10-19-plan105-replay-parser-bug-blocked.md`.
