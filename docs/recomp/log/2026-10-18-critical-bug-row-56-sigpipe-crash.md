- 2026-10-18 — hardware (main session + `hw-test`, `wt-hw`, branch `hw/next`):
  Test A rerun, now that #54's fix had landed. Follow mode confirmed working
  client-side (view tracked the scanner pose visibly). But stalling the
  client's JS thread for 3s to exercise #41's drop-gate **crashed the entire
  server process** — `SIGPIPE` (signal 13), exit 2, zero teardown lines,
  every client's session lost, not just the stalled one
  (`tests/results/2026-10-18-scan-server-backpressure-follow-smoke-rerun.md`
  + addendum). **Root-caused in source**: `ws_broadcast_server.cpp`'s
  `send()` calls never set `SO_NOSIGPIPE`, and nothing in the process
  suppresses `SIGPIPE` globally — a write to a peer that reset mid-send (the
  100ms `SO_SNDTIMEO` abort) raises `SIGPIPE`, which on macOS terminates the
  process by default. Filed as **#56**, `TODO`. #41's `SO_SNDTIMEO` protects
  the broadcast loop from blocking but not the process from dying — this is
  worse than the thing it was meant to fix. No further hardware runs
  attempted; the device was left untouched.
