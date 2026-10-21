# 2026-10-21 — the Claude batch: intake, the two follow-up recons, and the band-rule correction

The hardware tester's 2026-10-20 Claude batch was intaken in three passes (the
streaming gates, the #132 capture bundles, the A/B/observation records). This entry
records what the intake found and what was landed; the board carries the row state.

## The batch

15 records + 17 raw logs. **10 records landed in PR #430** with their raw logs; the
`plan132-gapfree-captures` record landed separately in **PR #434** (it is #433's
evidence, and holding it had made #433's tally un-citable); four records remain with
the tester for repair:

- `streaming-retry-167-161-168` — no `count=1`/`scan_server` output in its own raw
  log; its #167 `DONE` proposal contradicts the 5/5 `count=0` record.
- `four-new-selftests` — md timings disagree with the log; the filtered `-R` ctest
  run is missing.
- `plan151-content-cadence` — `scan_pause` absent from the truncated log; 34.2 Hz
  uses 90 s while the wall-clock span is 95 s (32.4 Hz); 20 perf windows vs 19.
- `plan71-attempt9-ab` — the first-6-of-17 table mean does not recompute.

## What the batch proved

- **#167**: the crash is fixed 5/5 (clean exit 2), but 30 probes across five starts
  all returned `count=0` while `DeviceManager` logged `list: (1)`. The retry budget
  expires; a power-cycle clears it. This is **#173**.
- **#161**: the 5 s bound fires (`kind=Timeout`, exit 3); the depth data was later
  captured post-power-cycle. `DONE`.
- **#168**: the capture path no longer hangs and writes its manifest; `DONE`.
- **#132**: IR 180/180 and depth 200/200 data replies, whole `Bin(2304000)` frames.
  The band rule was the open half; **#174**.
- **#76/#160**: the coverage set responds (`visited` 250->2047, `underCovered`
  19840->13789, `clients=1.0`); `DONE`.
- **#93**: the server-side markerless sequence PASS; the HUD was not observed
  (`clients=0`), so it stays `HW`.
- **#151**: the device half PASS (~32.4 Hz device vs 9.73 Hz publish, 3.5:1, 100 %
  tracking, UI closed); the browser render half is owed.
- **#71**: attempts 7-9 accept the toggle both ways but show no point-count
  step-change (A 19,734 vs B 19,832, Δ98/0.5 %, within variance); the effect is not
  confirmed.

## The two follow-up recons

- **#173** (PR #432, merged `a80ff71`): the two-path model — device-manager callbacks
  list the device, the synchronous `get_device_num` returns 0 — and the
  **hypothesis** that RPCServer holds the device claimed after a client session. The
  power-cycle is confirmed; `pkill -f RPCServer` is untested and is the operator test.
  The row stays `RECON`.
- **#174** (PR #433, merged `4e53ef1`): the band rule is **falsified**. The session4
  capture used BPF `less 16000`, so every TCP segment >= 16 KB was dropped; the 12 KB
  "bands" were segment fragments. One RPC reply is one whole 2.3 MB frame (379/380;
  one 1,321,920 B anomaly at msgid 914). No client-side band assembly. `DONE`; the
  pixel scale remains #126.

## Still owed

- The four record repairs (the tester).
- #173's device test: `scan_server --live` to exit 2, `pkill -f RPCServer`, power-cycle;
  capture `ps`/`ioreg` at each step.
- #93's HUD observation on port **8787** with the browser open; #151's browser render
  half; #170's live claim/release; #169's USB descriptor capture; #165's
  `_TEMPLATE.md`; the T4/T5 captures and #126.
- #171/#172 stay held behind #169; #100 remains the Phase D parent.
