# PLAN #132 — the gap-free capture (IR + depth) — BLOCKED

- Date: 2026-10-20
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_client/raptor_client (build-tree copy (fixed order: build/tools/<target>/<target>))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 2026-10-20 01:11:43 >= newest source capture.cpp 01:11:38
- Interposer: n/a — links no vendor SDK (socket/STL only); vendor-artifact & interposer rules do not apply
- Run start: 2026-10-20 01:16:28
- Device: Creality Raptor Pro, CrealityScan daemon running, device enumerated, no other SDK user

## Commands

```
./build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 30 --capture ~/captures/plan132-ir --capture-seconds 30 --capture-bytes 500000000
./build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 30 --capture ~/captures/plan132-depth --capture-seconds 30 --capture-bytes 500000000
```

## Result

- Exit code: `1` for both (IR and depth)
- Verdict: **BLOCKED** — both captures aborted immediately after connecting, before any frame reply was ever received. Same underlying failure as #127 (`2026-10-20-plan127-live-reply-dump.md`): `ob_rpc_start_stream` rejected by the daemon.
- Raw log: `2026-10-20-plan132-gap-free-capture.raw.log` (same directory; both commands' output)
- Capture bundles written (outside the repo, per P1's convention):
  - IR: `~/captures/plan132-ir/` — `manifest.json` + `replies/port-10013.bin`
  - Depth: `~/captures/plan132-depth/` — `manifest.json` + `replies/port-10010.bin`

## Manifest + hash chain (both bundles)

**IR** (`~/captures/plan132-ir/manifest.json`):
```
record_count=1, bytes_written=140, limits_hit: seconds=false, bytes=false
files: [{"path":"replies/port-10013.bin","port":10013,"size":152,
         "sha256":"56ccef1cbe47ef0cff80d1a0ee3343eda18e9be1c7296b6f4613b393c89bffba",
         "records":1}]
commit: efe9253614209775f066992369d52c88d49076b4
binary_sha256: 5c1c40dd82697b3635e649e75119f99b8ec580040d543c783825d7f881b69a47
```

**Depth** (`~/captures/plan132-depth/manifest.json`):
```
record_count=1, bytes_written=140, limits_hit: seconds=false, bytes=false
files: [{"path":"replies/port-10010.bin","port":10010,"size":152,
         "sha256":"8c8db4aa1774062bd7d4b21039734eb333c6bbec7150d6f595eb6adcb5bd8b36",
         "records":1}]
commit: efe9253614209775f066992369d52c88d49076b4
binary_sha256: 5c1c40dd82697b3635e649e75119f99b8ec580040d543c783825d7f881b69a47
```

**Was every reply captured?** Yes, trivially — both bundles have exactly 1
record (the `ob_rpc_start_stream` error reply itself) because the client
aborted immediately after that single rejected call; neither 30s bound nor
the 500MB bound was hit (`limits_hit: {"seconds": false, "bytes": false}`
in both manifests). This is not the gap-free frame-stream capture #132
needs — it's a 1-record capture of the same failure #127 hit.

## On failure or block — observable + one lead

- Observable: both commands print `[live] start_stream failed: rpclib:
  server could not find function 'ob_rpc_start_stream' with argument count
  5.` before any streaming begins, identical text to #127's failure on the
  same daemon.
- Lead: same as #127 — `ob_rpc_start_stream`'s argument count at the call
  site (`tools/raptor_client/main.cpp:912`) no longer matching this
  daemon's expectation. Not re-diagnosed here; this is the same root
  blocker surfacing in a second tool path.

## Crash sweep

`scripts/hw-postflight.sh 2026-10-20-plan132-gap-free-capture '2026-10-20 01:16:28' raptor_client`:

> hw-postflight: no crash report for 'raptor_client' at/after 2026-10-20 01:16:28 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## Open questions

- Same as #127: has the daemon's `ob_rpc_start_stream` argument count
  changed, or is this a pre-existing mismatch not previously exercised on
  this daemon version? Not determined here.

## Board

- Row move I would propose for **#132**: stays `TODO`/not unblocked —
  the capture tooling itself (manifest, hash chain, bounds) worked exactly
  as designed, but it could only capture the one failure reply since
  streaming never started. This is blocked on the same root cause as
  #127, not a separate issue — fixing that one blocker likely unblocks
  both.
