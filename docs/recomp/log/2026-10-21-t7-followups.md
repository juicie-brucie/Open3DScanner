# 2026-10-21 — the T7 follow-ups: records landed, the clock correction, the fixes

## The records

- **#457** — the T7 batch: the #178 probe PASS, the #171/#178 capture BLOCKED
  (OQ-169e), the #172 offline A/B PASS, the #179 live BLOCKED (claim/XPC, OQ-169e, OB
  words), the #175 IR/depth telemetry, the session audit addendum.
- **#462 + #460** — the T2 records with same-stem raw extracts and their two correction
  addenda; the #175 depth addendum + its six-value correction; the IR telemetry correction
  (53 data frames; the `prop=192` anomaly). With both PRs in, the T2 record set is complete
  and corrected.
- **#458** — the board (the T7 results and #180–#183).

## The daemon code

- **#461 (#181)** — the user-scope helper: a user LaunchAgent registering
  `org.openscanner.helper` as a Mach service, dev-signed, **no sudo**;
  `raptor_shim --helper-status`; the 14-check real-XPC test. The operator's install + the
  no-sudo #170 four-step test is the next card.
- **#459 (#182)** — the probe's correlation mode: `--probe --match`/`--watch` read the
  IORegistry names only (no open/seize), resolving **IR = interface 3, depth = 1, RGB = 5,
  HID/IMU = 6** with the vendor daemon holding the device. Formats/stride still need the
  opening probe with the daemon stopped.
- **#184** — the `prop=192` anomaly: the client's props array is `{3,152,153,154,155}`, yet
  the IR raw log line 218 shows the 152 slot sent as `192` at `reqid=192` (only occurrence;
  `prop == reqid`). A client-side cause to find and fix.

## The e1/e2/e10 correction (#180 / #183)

The #180 builder decoded all **380** `get_frame` replies in the #132 bundles before writing
code: **e1 is never 0** (a `CLOCK_REALTIME` µs stamp), **e2 is monotonic**
(`CLOCK_MONOTONIC` µs), **e3 = 255**, and **e10 is mixed 3/4 on both interfaces**. The #143
"e1 = 0" claim was wrong (it generalised from the no-data form); #183 corrected
`pull-semantics.md` §2 and the #172 `[10]` sample note. The interface mapping is
hw-confirmed by #182. The dimension mismatch (1920×1200 RPC vs the lower-resolution UVC VS)
stays open, as does the OBFormat source for e3.

## The CI

The `verify-pr` `ctest exit 8` recurred across PRs. The checker's `sh()` captures the ctest
output, so the CI log names no failing test. A fresh local build of #459 passed **30/30**,
pointing at the restored `build-*` cache; ~100 build caches were cleared. A checker
improvement — print the ctest output on failure — is a follow-up (the checker in CI is the
origin/main copy, so it needs its own PR).

## The roster

verify4 was in **DRIFT**: a local `pr-461` branch plus a `Testing/` ctest artifact. The slot
was returned to `verify4/next`, the artifact removed and the branch deleted; `check` is
PASS. The prune deleted 28 local merged branches (the remote copies were already
auto-deleted on merge). Two unclassified branches await the user's decision:
`recon1/143-fix`, `recon2/143-pull-semantics`.

## The operator cards

1. The opening descriptor probe (formats/stride; the daemon stopped).
2. The no-sudo #170 four-step test via the #181 user agent.
3. The live IR/depth capture with the vendor agent booted out.
4. The vendor A/B; the switch (default off).

## Pending

#459 (the CI re-run), #463 (the consistency re-read), #184 (the prop anomaly).

swarm: dispatch -> ses_ea4369866ffevkbXX2DjXY2Qx4 | plan ->
ses_ea434c425ffeTXs2xjdYTWvbKh | recon1 -> ses_ea764fc29ffeH68CjSl6fUFMKU | recon2 ->
ses_ea4338f96ffe5qVzHFl0bUNmiV | build1 -> ses_ea7817175ffe40vyuvwNLc77RR | build2 ->
ses_ea433905affedBdErlWhZKtSAx | build3 -> ses_ea40e077dffevzEiLXzMSMyFdN | verify1 ->
ses_ea0ac2078ffez3CJi3xbgjdjPe | verify2 -> ses_ea7817014ffeqRw5157SNi10d1 | verify3 ->
ses_ea7816f64ffeAaBIRE1BIy2YFZ | verify4 -> ses_ea01f5965ffeZ6KSDI1JBV8EC8
