# PLAN #127 — raptor_client --live --dump-replies (the #105 "what does get_frame carry live" question) — BLOCKED

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_client/raptor_client (build-tree copy (fixed order: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 2026-10-20 01:11:43 >= newest source capture.cpp 01:11:38
- Interposer: n/a — links no vendor SDK (socket/STL only); vendor-artifact & interposer rules do not apply
- Run start: 2026-10-20 01:15:52
- Device: Creality Raptor Pro, CrealityScan daemon running, device enumerated, no other SDK user

## Command

```
./build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 5 --dump-replies
```

## Result

- Exit code: `1`
- Verdict: **BLOCKED** — the live call failed before any `get_frame` reply could be dumped.
- Raw log: `2026-10-20-plan127-live-reply-dump.raw.log` (same directory)

## On failure or block — observable + one lead

- Observable:
  ```
  [live] connected 127.0.0.1:10013 (pull_ms=0, per-call logging=on)
  [dump] reply method=ob_rpc_start_stream kind=Error msgid=1 envelope=Array result=Error(rpclib: server could not find function 'ob_rpc_start_stream' with argument count 5.)
  [live] start_stream failed: rpclib: server could not find function 'ob_rpc_start_stream' with argument count 5.
  ```
  The TCP connection to the daemon succeeded; the very first RPC call
  (`ob_rpc_start_stream`) was rejected by the daemon itself with an
  argument-count mismatch, and the client aborted before reaching
  `get_frame` — so this run supplies no reply-dump data at all for #127's
  question (kind/msgid/array length/element kinds/bin sizes — none of it
  was ever reached).
- Lead: likely in `ob_rpc_start_stream`'s argument list/count at the call
  site (`tools/raptor_client/main.cpp:912`) no longer matching what this
  daemon/firmware version expects — not diagnosed further here.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-20-plan127-live-reply-dump '2026-10-20 01:15:52' raptor_client`:

> hw-postflight: no crash report for 'raptor_client' at/after 2026-10-20 01:15:52 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## Open questions

- Has the daemon's/firmware's `ob_rpc_start_stream` argument count changed
  since this call site was last verified against live hardware, or is
  this a pre-existing mismatch that simply hasn't been exercised on this
  exact daemon version before? Not determined here.

## Board

- Row move I would propose for **#127**: stays `HW`, not answered —
  blocked before reaching the question it was meant to answer. Needs the
  argument-count mismatch resolved (builder/recon) before a rerun can
  supply the reply-dump data.
