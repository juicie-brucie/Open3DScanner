- 2026-10-19 — hardware (`hw-test`, `wt-hw`, branch `hw/next`): #98's first
  live run — `raptor_client`, the vendor-free Phase B client, against the
  real daemon + device for the first time. `--dry-run` and `--discover`
  both PASS (5/5 framing checks; a genuine 256-byte DISCOVERY_ACK with
  readable device strings). `--live 127.0.0.1 10013 5` ran clean end-to-end
  — connect, `start_stream` accepted, full 5s loop uninterrupted, `stop`
  sent normally, no leaked stream — but pulled **zero frames**, exit 1 (the
  documented not-a-crash zero-frames case). No RPC-level error anywhere,
  but this build has no per-call diagnostics for `ob_rpc_get_frame` or the
  laser/exposure `SetProperty` calls, so accepted-vs-silently-rejected
  can't be told apart yet. Mechanically sound, functionally incomplete.
  Filed as **#105** (`TODO`) — per-call logging + re-deriving the request
  shape against the capture set. #98 stays `HW` pending real frames.

  Addendum (dispatcher): the operator's live screenshot of the **vendor** path (the app's own scan,
  same daemon) shows a full stream — 245,235 points, `tracking SUCCESS (0)`, `coverage 0% covered,
  6667 under-covered pts` — so the daemon serves frames fine and #105's gap is `raptor_client`'s
  request shape/sequencing. `[inferred — screenshot, not a measurement of raptor_client]`
