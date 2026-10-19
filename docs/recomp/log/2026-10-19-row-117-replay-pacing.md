- 2026-10-19 — build-3 (build room, `wt-build3`, branch `build/plan117-replay-pacing`): **#117's
  two mechanisms are confirmed and fixed**; the code is ready for the hardware re-run.
  The board's claim held up on the evidence: the `.hex` `+NNNNN` prefixes are **absolute capture
  timestamps**, not per-frame delays — all 36 match `docs/recon/findings/streaming-replay-payloads.md`'s
  `t=25.xxx` table to the millisecond (25.259529 s → `+25259`, 25.261083 s → `+25261`, …), and the
  inter-arrival gaps are 0–45 ms, not the ~25 s the old code slept before each frame.

  **(1) Pacing.** `ParseReplayFile` now records the prefix as a *timestamp*, and the replay sleeps the
  **inter-arrival delta** between consecutive stamps. The first frame's wait is measured from the
  capture's t=0 and is **capped at 250 ms** — that offset is recon's clock, not the daemon's, so there
  is nothing to wait for. A stamp that goes *backwards* clamps to 0 ms (the capture file groups frames
  by call type, not by time, so it is not monotonic) and the count is reported. The request bytes are
  untouched.

  **(2) Bounded read.** `rpc.readReply()` was an unbounded `ReadMessage()`; the replay path now uses
  `readReplyWithDeadline()`, a `poll()`-based twin whose deadline covers the *whole* reply (a reply
  that dribbles in forever still times out). `--reply-timeout-ms` (default 2000) sets it; on expiry the
  replay prints `[replay] frame N: no reply within <ms> ms` and exits non-zero. The **live path is
  unchanged** — `call()` still uses the unbounded read.

  **Dry-run diagnostic** (the no-hardware cadence check):
  `[replay] cadence: 36 frame(s), capture span 256 ms, mean gap 10.74 ms, first-frame wait 250 ms (backward stamps clamped to 0)`

  **Reproduced both failures locally, without the device.** Against a mock daemon that replies
  `[1, msgid, nil, bin(3)]`: the **old** build sent 2 frames in 60 s and would need ~15 minutes for
  36 (it slept the absolute stamp before every frame); the **new** build completes **36/36 in ~1 s**.
  Against a peer that accepts and never replies: the old build **hangs indefinitely**; the new build
  fails after the deadline, naming the frame.

  **Selftests** (`--dry-run`, plus a new `--selftest-timeout`), both registered with ctest:
  `raptor_client_selftest` pins the conversion on the capture's own first three stamps (`+25259`,
  `+25261`, `+25281` → sleeps 25259, 2, 20 ms) and the first-frame cap; `raptor_client_reply_timeout`
  drives the bounded read against a silent loopback peer. The selftest caught a real bug in the first
  draft of the parser (the timestamp accumulator started from the "no timestamp" sentinel, so the
  first digit corrupted every value) — it is fixed and the tests are green.

  **For the hardware re-run (the acceptance):** the replay must either complete **36/36** or fail
  naming the exact frame. One thing the hw tester should know, found while reading the file: the
  capture **interleaves two interfaces** — 13 frames carry the `0-2-12` (IR, port 10013) method
  suffix and 23 carry `0-2-10` (depth, port 10010) — but `Replay()` has a single connection to one
  port. Whether the daemon answers regardless of the method-name suffix is exactly what the run
  should observe; the reply counts are not yet split per port.
