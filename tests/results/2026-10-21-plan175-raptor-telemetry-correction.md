# Correction addendum — 2026-10-21-plan175-raptor-telemetry.md

Date: 2026-10-21
Corrects: `2026-10-21-plan175-raptor-telemetry.md`

## Defect 1 — Check 2 label conflates seconds argument with frame count

Line 34 of the original:
> `| 2 | IR port delivers 8 data frames | **[PASS]** ...`

`8` is the `seconds` argument to `raptor_client --live 127.0.0.1 10013 8` (run
for 8 seconds), not the data frame count. The actual data frame count is 53
(confirmed: `done: get_frame=56 data=53 empty=3 error=0`).

Corrected check row:
> `| 2 | IR port delivers data frames (data share > 0) | **[PASS]** done: get_frame=56 data=53 empty=3 error=0; stream stopped (53 data frames across 56 polls) |`

## Defect 2 — prop=192 anomaly not documented

Raw log line 218 (verbatim):
```
[live] SET prop=192 value=0 reqid=192 -> rpc kind=5
```

In the surrounding cycle, the normal sequence is `prop=3 / prop=152 / prop=153 /
prop=154 / prop=155`. At reqid=192, `prop=192` appears instead of `prop=152`.
This is the only occurrence of `prop=192` in the 56-call IR run (confirmed:
`grep -n "prop=192"` finds line 218 only). The prop number (192) equals the
reqid (192) — whether this is a coincidence, a typo in the client, or a
genuine prop=192 send is not determined here.

This anomaly was not documented in the original record. It is observable and
should be noted for the fixer or recon.

## Verdict and board

No change to verdict (FAIL) or board move (#175 stays HW). The IR port PASS
checks and depth port FAIL check are unaffected.
