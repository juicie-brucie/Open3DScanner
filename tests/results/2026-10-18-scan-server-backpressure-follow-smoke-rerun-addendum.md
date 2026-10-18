# 2026-10-18 — addendum to `2026-10-18-scan-server-backpressure-follow-smoke-rerun.md`: client-side correlation for the SIGPIPE crash

This addendum does not edit the original record in place; it adds
client-side evidence supplied by the coordinator session after that record
was written, per the policy of never editing a written record after the
fact.

## What the coordinator (main session) reported, verbatim-summarized

Driving a real browser tab against the live UI (port 8788) as the WebSocket
client for this run, the coordinator reported this sequence:

1. Connected; toggled follow mode — confirmed working (camera visibly
   tracked the scanner pose as it moved).
2. Waited ~10s; client frame counter reached ~893, points ~17,330.
3. Ran a 3-second busy-loop blocking the browser tab's main JS thread via
   `javascript_tool` (`const end = performance.now() + 3000; while
   (performance.now() < end) {}`) specifically to exercise the drop-gate.
4. Immediately after the block ended, the browser console showed
   `WebSocket connection to 'ws://127.0.0.1:8787/' failed: Invalid frame
   header`, followed by a flood of repeated reconnect failures; the page
   went to `disconnected`, frozen at frame #1055.
5. Port 8787 had no listener and no `scan_server` process existed anymore
   when checked — consistent with this run's server-side `SIGPIPE`/exit-2
   finding.

**I did not independently observe steps 1–5 above** — they are the
coordinator's report of what their own browser tab and JS console showed,
which is outside what this server-side vantage point can see. I'm recording
them here as reported evidence, attributed to the coordinator, not as
something I directly confirmed.

## Correlation with the server-side record

This client-side account is consistent with, and corroborates, the
server-side finding already filed in
`2026-10-18-scan-server-backpressure-follow-smoke-rerun.md` row 4 (flagged
as "possible #41-adjacent new finding, not confirmed" at the time): the
server's tick log ran cleanly through tick 500 (`clients=2`, `paused=0`,
no gaps) and then the process died to signal 13 (SIGPIPE) with no teardown
lines ever printed. The coordinator's reported timeline — JS-thread stall
ends, WebSocket immediately reports a malformed-frame error, then a flood
of reconnect failures, then the server process is simply gone — lines up
with the server-side timing (crash immediately follows the last tick line
observed in the raw log).

## Resulting read of the causal chain (coordinator's proposed explanation, reported here, not independently verified by this session)

Client goes briefly unresponsive (JS thread blocked) → the server's write
to that client's socket hits the 100ms `SO_SNDTIMEO` and aborts mid-frame →
the socket is left in a bad state (consistent with the client later seeing
"Invalid frame header" — a partially-written frame) → a subsequent write to
that same now-broken socket raises `SIGPIPE` with no handler/suppression in
the server (no `SO_NOSIGPIPE`/`MSG_NOSIGNAL` evident from the observable
behavior) → the whole worker process dies, taking down **every** client's
session, not just the stalled one.

I have not read the server's socket-handling source to confirm the
`SO_NOSIGPIPE`/`MSG_NOSIGNAL` detail — that would require reading
`tools/scan_server/main.cpp`'s send path, which I have not done beyond what
was already cited in the prior run's record for the teardown-logging
condition. The mechanism above is the coordinator's proposed explanation,
and it is plausible and consistent with every piece of evidence both
sessions gathered, but I'm not independently certifying the exact code-level
cause — only that the observed behavior (clean run to tick 500, process
gone right as/after the client-side stall ended, no teardown) is consistent
with it.

## Status

Per the coordinator's instruction, **no retry was performed**; the
coordinator is filing this as a new critical bug on the board rather than
rerunning. This addendum, together with
`2026-10-18-scan-server-backpressure-follow-smoke-rerun.md` and its raw log
`2026-10-18-scan-server-backpressure-follow-smoke-rerun.log`, is the full
evidentiary record of this attempt.
