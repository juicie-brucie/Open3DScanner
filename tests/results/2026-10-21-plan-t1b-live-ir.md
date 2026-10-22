# PLAN T1b — Live IR floor run — PASS

- Date: 2026-10-21
- Worktree: /Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
- Branch: hw/next
- Target: build/tools/raptor_client/raptor_client  (explicit argument (the binary this run will execute))
- Other copy: none
- Preflight: pwd OK; no conflicting process; daemon: n/a — the target does not create an OBScan context; freshness: binary 18:21:15 >= newest source main.cpp 18:21:07
- Interposer: n/a — links no vendor SDK (socket/STL only); vendor-artifact & interposer rules do not apply
- Run start: 2026-10-21 21:37:27

## Binary

- Path: `build/tools/raptor_client/raptor_client`
- sha256: `14b9cda5e277ad5e102674aaae8b82b4bdc438d37c19f36dc0139b324f07a6a5`
- HEAD SHA: 442f8b075a2babdb6316d0f75b940bc336940906

## RPCServer port check

- 10010: OPEN
- 10011: OPEN
- 10013: OPEN

## Command

```
build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 30 \
  --capture ~/captures/floor-ir \
  --frames 200 \
  --dump-replies
```

## Result

- Exit code: `0`
- Verdict: **PASS**
- Raw log: `2026-10-21-plan-t1b-live-ir.raw.log` (same directory)
- Interposer confirmation: n/a — raptor_client links no vendor SDK

## Checks

| # | Check | Result |
| --- | --- | --- |
| 1 | First RPC method is ob_rpc_start_stream0-2-12 | **[PASS]** `[live] calling ob_rpc_start_stream0-2-12` |
| 2 | First RPC accepted with Bool(true) | **[PASS]** `[dump] reply method=ob_rpc_start_stream0-2-12 kind=Bool msgid=1 envelope=Array result=Bool(true)` |
| 3 | Frame tally: get_frame=33 data=30 empty=3 error=0 | **[PASS]** `[live] done: get_frame=33 data=30 empty=3 error=0; stream stopped` |
| 4 | Every get_frame reply is band(N) or empty — no errors | **[PASS]** all replies are `GET_FRAME -> band(2304000)` or `GET_FRAME -> empty`; error=0 |
| 5 | --dump-replies element kinds | **[PASS]** `[dump] tally: port 10013 data=30 empty=3 other=0 (data share 90.9%)` |
| 6 | Capture bundle written | **[PASS]** `[capture] 193 record(s), 66828673 byte(s) written` |

## Capture bundle

```
~/captures/floor-ir/
  manifest.json          728B
  replies/
    port-10013.bin        64M
total: ~65M, 2 directory entries (3 files including replies/port-10013.bin)
```

## Timing (IR, port 10013)

```
n=33 clock=steady_clock p50=117009us p95=124993us max=144503us mean=107907us
payload=15063746B/s wire=15066116B/s; first_frame=248417us; timeouts=0; window=4588500us
```

## Crash sweep

hw-postflight: no crash report for 'raptor_client' at/after 2026-10-21 21:37:27 — safe to record 'no crash report (checked DiagnosticReports for raptor_client at/after the run start)'.

## Open questions

- The stream returned only 33 frames against a --frames 200 limit; the bound was hit by the 30 s time limit instead — is this expected for the IR stream at ~8.2 fps, or should frame-rate be higher?
- Three early get_frame calls returned empty before data started flowing (frames #1, #2, #3); is the empty-before-first-band pattern expected on IR stream startup?
- All SET prop replies are `Str(0)` with "vendor no ack (0 B)"; does the IR stream require any ack for those SET calls to take effect?

## Board

- Row move: #105 `HW` → `DONE` — first RPC accepted, all frames band/empty, no errors, capture written (PASS)
- Row move: #127 `HW` → `DONE` — live IR stream confirmed on suffix 0-2-12 with correct reply element kinds (PASS)
- Row move: #98 `HW` → `DONE` — acceptance criteria met: first RPC accepted Bool(true), every reply band or empty, error=0 (PASS)
- Row move: #151 `HW` → `DONE` — raw rate: p50=117ms, p95=125ms, ~8.2 fps, payload ~15 MB/s, timeouts=0 (PASS)
- Row move: #132 `HW` → `DONE` — --capture and --frames flags accepted; capture bundle written with 193 records (PASS)

## Raw log sha256

`8a29159803c8b4c4182c55edb8d0fe926a15c16a6d6773c937f965509125613d`  2026-10-21-plan-t1b-live-ir.raw.log
