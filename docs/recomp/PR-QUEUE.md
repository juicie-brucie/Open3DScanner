# PR landing queue

Owner: dispatch. One landing in flight; the GitHub merge queue is not used.

## Queue — 2026-10-21 (after the fresh hardware batch)

| order | PR | row | state | next |
| --- | --- | --- | --- | --- |
| 1 | #451 | vendor-stop note | docs; CI re-run | land |
| 2 | #452 | board: fresh batch | script | land |
| — | #177 | privilege model | RECON, recon1 | finding |
| — | #178 | descriptor probe + live source | BUILD, build1 | PR |
| — | #179 | live shim + switch flag | BUILD, build3 | PR |

## Landed this batch

| PR | what |
| --- | --- |
| #450 | the 2026-10-21 records: #170 live gate, #141 v2, #167 2nd, #105/#127 depth, #153, the repairs |
| #449 | the previous wave's closure |
| #437–#448 | the telemetry/retro/USB code wave |

## Held / owed

- **#93 HUD** and **#151 browser** records: the tester repairs (rendered line; clock and
  citation); the rows stay `HW`.
- **#153 picture**: the direct comparison is unavailable; `HW`.
- **#169** the USB descriptor capture (or #178's `--probe` run); **#170's no-sudo test**
  (#177); the vendor-agent stop before each run (the HW-queue commands).
- The `_TEMPLATE.md` half of #165 (operator).
- #171/#172/#178/#179: the live source, the vendor A/B, the switch.

A PR is added here when it opens; it leaves when merged or closed.
