# PLAN C — vendor depth/ack contrast, ports 10010 vs 10013 (#187) — PASS

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_client/raptor_client (build-tree copy (fixed order 2: build/tools/<target>/<target>))
- Sha256: `14b9cda5e277ad5e102674aaae8b82b4bdc438d37c19f36dc0139b324f07a6a5`
- HEAD: `e7e1ad200b4ff10a86008ec723bd53ff78503e62`
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 18:21:15 >= newest source main.cpp 18:21:07
- Interposer: n/a — `raptor_client` links no vendor SDK (socket/STL only); exempt per preflight
- Run start: 2026-10-21 22:23:42
- Peer: vendor RPC (RPCServer), freshly restarted for this run — `launchctl bootstrap gui/501 /Library/LaunchAgents/com.creality.RPCServer.plist` after the earlier bootout in the UVC run; confirmed running alone (pids 8585/8587, `/Library/Creality/RPCServer`) before either command

## Commands

```
build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 8 --frames 10 --dump-replies
build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 8 --frames 10 --dump-replies
```

## Result

- Exit codes: both `0`
- Verdict: **PASS**
- Raw logs: `2026-10-21-plan-c-vendor-10010.raw.log`, `2026-10-21-plan-c-vendor-10013.raw.log`

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | Port 10010 SET ack form | **[PASS]** `vendor ACCEPT (opcode=2 reqid=NN err=0, 64 B)` — reply head `52 42 01 00 02 00 ...` ('RB' str, 64 bytes), every SET on this port |
| 2 | Port 10010 data share | **[PASS]** `[live] done: get_frame=10 data=1 empty=9 error=0`; `[dump] tally: port 10010 data=1 empty=9 other=0 (data share 10.0%)` |
| 3 | Port 10013 SET ack form | **[PASS]** `vendor no ack (0 B)` — `result=Str(0) head=[]`, every SET on this port |
| 4 | Port 10013 data share | **[PASS]** `[live] done: get_frame=10 data=7 empty=3 error=0`; `[dump] tally: port 10013 data=7 empty=3 other=0 (data share 70.0%)` |
| 5 | first_frame timing | **[PASS]** port 10010 `first_frame=274us`; port 10013 `first_frame=223983us` |

The two ports differ both in their SET-ack form (64 B 'RB' string vs 0 B) and in GET_FRAME data share (10% vs 70%) — recorded as observed, not interpreted further.

## Crash sweep

`scripts/hw-postflight.sh plan-c-vendor '2026-10-21 22:23:42' raptor_client` →
`hw-postflight: no crash report for 'raptor_client' at/after 2026-10-21 22:23:42 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.`

## Open questions

- Does the 64 B 'RB' ack on 10010 vs the 0 B ack on 10013 map to a documented control/data channel split, or is this the first observation of it?
- Is the 10013 empty-frame share (3/10) stable across repeated runs, or does it vary with time-since-start (`first_frame=223983us` is far later than 10010's `274us`)?

## Board

- Row move: none proposed by this record — reporting the numbers and what they establish only.
