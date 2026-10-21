# PLAN #132 — IR capture, second attempt after a confirmed-good connection check — HUNG again, narrows the cause to --capture itself; depth capture held

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- HEAD: `fc6a081`
- Target: build/tools/raptor_client/raptor_client (build-tree copy)
- Preflight: pwd OK; no conflicting process; freshness: binary 2026-10-20 03:54:34 >= newest source main.cpp 03:54:28
- Interposer: n/a — links no vendor SDK
- Run start: 2026-10-20 17:56:50
- Device: Creality Raptor Pro, daemon running, device enumerated, no other SDK user
- Context: retried after the user confirmed the physical connection
  directly. `--dump-replies` was deliberately dropped from this attempt
  (present in the first IR-capture hang) to isolate which flag the hang
  is tied to.

## Command

```
./build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 60 --frames 200 --capture ~/captures/plan132-ir-retry3 --capture-seconds 60 --capture-bytes 500000000
```

## Result — HUNG again, same signature, with --dump-replies removed

- Exit code: `143` (`SIGTERM`, sent by this agent)
- **Observable**: zero bytes of output, process alive and idle, identical
  to the first IR-capture hang
  (`2026-10-20-plan132-gap-free-capture-retry.md`) — **even with
  `--dump-replies` removed this time.** This rules out `--dump-replies` as
  the trigger and narrows the hang specifically to the
  `--capture`/`--frames` code path itself (or its interaction with an
  already-active `--live` session), independent of port — this is now the
  **second** port (IR) to hang via `--capture`, after the depth hang
  earlier used the same flags.
- Raw log: `2026-10-20-plan132-ir-retry3-hang.raw.log` (0 bytes)
- **Not retried further** on IR, per the no-retry-on-hang rule.
- **Depth capture was not attempted** this round — given `--capture`
  itself now looks like the broken mechanism (confirmed hanging twice on
  IR with two different flag combinations, plus once on depth with the
  same flags), running the identical known-broken code path against depth
  again would very likely just reproduce the same hang without new
  information. Held rather than spent on a near-certain third hang —
  flagging for a decision rather than running it.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-20-plan132-ir-retry3-hang '2026-10-20 17:56:50' raptor_client`:

> hw-postflight: no crash report for 'raptor_client' at/after 2026-10-20 17:56:50 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## Open questions

- Confirmed not caused by: `--dump-replies` (removed, still hung), port
  choice (IR hung here; depth hung earlier with the same flags), or a bad
  physical connection (user-confirmed good immediately before this
  attempt).
- Likely candidate, not confirmed: something in the `--capture` code
  path's setup (opening the capture directory/manifest, or how it
  interacts with the frame-pull loop under `--frames N`) stalls before
  the first byte of any output — including before the normal `[live]
  connected...` line that every non-`--capture` run prints immediately.
  Worth a source-level look (not done here, per role) at whatever runs
  between process start and the first print statement when `--capture`
  is set.

## Board

- Row move I would propose for **#132**: stays open/unresolved — now with
  much stronger evidence than the first attempt that **the `--capture`
  code path itself is the broken component**, not a specific interface or
  the daemon connection. Recommend a builder-level look at
  `tools/raptor_client/capture.cpp`'s startup path before any further
  hardware attempts — a code-level read is likely to find this faster
  than more hardware retries, which have now twice reproduced the exact
  same zero-output signature.
