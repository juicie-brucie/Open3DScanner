# #175 — vendor-RPC depth-first, fresh RPCServer (the 64-B ack hypothesis) — FAIL

- Date: 2026-10-24
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_client/raptor_client  (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 18:21:15 >= newest source main.cpp 18:21:07
- Interposer: n/a — links no vendor SDK (socket/STL only); vendor-artifact & interposer rules do not apply
- Run start: 2026-10-24 07:55:38

## Preconditions

- Vendor daemon: restarted **fresh** immediately before this run, depth port as the very first call in the new daemon lifetime, per `docs/recon/findings/live-ack-depth.md` §4's named test — `launchctl bootout gui/501/com.creality.RPCServer` (rc 3, not running), then `launchctl bootstrap gui/501 /Library/LaunchAgents/com.creality.RPCServer.plist` (rc 0; pids 64256/64260). No vendor app, no `scan_server`, no prior `raptor_client` session since that restart.

## Command

```
build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 8 --frames 10 --dump-replies
```

(exactly the test named in `docs/recon/findings/live-ack-depth.md` §4, carried by `#474`'s finding and named as the operator's "T8 card A" on row `#187`)

## Result

- Exit code: `1`
- Verdict: **FAIL**
- Raw log: `2026-10-24-plan175-depth-first-fresh.raw.log` (same directory)
- Interposer confirmation: n/a (this target links no vendor SDK)

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | `start_stream` returns true | **[PASS]** `[live] start_stream -> true` |
| 2 | Every SET's `ob_rpc_send_data` ack is **64 B** (the "RB" ack the gapfree capture showed, not the 0-B "live-run" ack from the 2026-10-21 falsified attempt) | **[PASS]** all 10 SET calls (`reqid=1`..`50`, props 3/152/153/154/155 each cycle) returned `Str(64)` with head bytes `52 42 01 00 02 00 ...` (ASCII `RB`, opcode 2, incrementing reqid, err=0) |
| 3 | Depth frames flow given the 64-B ack (the hypothesis this test exists to check) | **[FAIL]** every one of the 10 `get_frame` calls returned the empty 11-element array (`[0]=UInt(0)...[3]=UInt(255)...[4]/[6]/[8]=Bin(0)`, `kind=7`); `GET_FRAME -> empty` every time |
| 4 | Clean stop | **[PASS]** `ob_rpc_stop_stream0-2-10 -> Bool(true)` |
| 5 | Telemetry (#175's own instrument) | reported but not meaningful here: `n=10 p50=128us p95=168us max=168us mean=133us; payload=0B/s wire=87043B/s; first_frame=none; timeouts=0` |

## Crash sweep

hw-postflight: no crash report for 'raptor_client' at/after 2026-10-24 07:55:38 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## On failure or block — observable + one lead

- Observable: `data=0 empty=10 error=0` (`[live] done: get_frame=10 data=0 empty=10 error=0; stream stopped`); `[dump] tally: port 10010 data=0 empty=10 other=0 (data share 0.0%)`. This happened **despite** every SET ack being the full 64-B `"RB"` form (opcode 2, err=0) — the exact ack byte-pattern that `live-ack-depth.md` named as correlating with frame flow in the gapfree capture. This run reproduces that ack form on a fresh, depth-first daemon and still gets zero data frames.
- Lead: the 64-B SET ack is **not** the gate for depth frame delivery — `docs/recon/findings/live-ack-depth.md` §3's correlation (64-B ack ↔ frames flow, 0-B ack ↔ no frames) does not hold under this fresh/depth-first run, which decouples the two: 64-B ack present, frames still empty. Whatever gated the gapfree capture's frames is something else — a device/session state not reproduced by a daemon restart, or a missing depth-specific setup call this client does not send (`#187`'s open question, still open after this run).

## Open questions

- Per `live-ack-depth.md` §4's own stated interpretation: this result matches neither of its two predicted outcomes cleanly — it predicted "64-B ack + frames flow" (gapfree-reproduced) or "0-B ack + no frames" (live-run persists); this run got **64-B ack + no frames**, a third outcome the finding did not anticipate. Does this falsify the ack-as-gate hypothesis outright, or does it mean the ack form and frame delivery are gated by two independent conditions, only one of which (the ack) this test satisfied?
- What is actually different between the 2026-10-20 gapfree capture (post-power-cycle, 64-B ack, frames flowed) and this run (fresh daemon restart, depth-first, 64-B ack, no frames) — is a device power-cycle (not just a daemon restart) the missing variable?
- Is there a depth-specific setup call (beyond the five SETs this client already sends: props 3, 152, 153, 154, 155) that the gapfree capture's producing process issued and this client does not?

## Board

- Row move: #175 stays `HW`. This is a reproducible, clean falsification of the "64-B ack gates depth frames" hypothesis from `#474`/`#187` — a fresh, depth-first daemon now gives the expected ack form but still no frames, narrowing the open question to a device/session-state variable (likely a power-cycle, not just a daemon restart) or a missing depth-specific call. Worth a fast-follow #187-family row for recon to update `live-ack-depth.md` with this third outcome.
