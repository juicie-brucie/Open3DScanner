- 2026-10-18 — hardware (`hw-test` + main session, two browser tabs, `wt-hw`,
  branch `hw/next`): #56's acceptance run — the identical 3s stall repeated
  with the SIGPIPE fix (PR #146) in place. **PASS**: zero `Broken pipe`/
  `SIGPIPE`/watchdog strings in the log, continuous tick cadence, clean
  teardown, exit 0. `clients=2→1→2` — client-side confirmed as a real
  prune+reconnect of the stalled tab, not a miscount; the control tab never
  lost its stream. #41's drop-gate is now confirmed safe under a real stall;
  #41 and #56 → `DONE`. Also captured #53's `backend` line for two of three
  paths (default `webgpu`, forced `webgl2`, both correct, matching final
  point counts) — the WebGPU-rejection path is still unexercised; #53 stays
  `HW` for that one item. No other hardware tests are currently queued.
