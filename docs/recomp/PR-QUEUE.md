# PR landing queue

Owner: dispatch. One landing in flight; the GitHub merge queue is not used.

## Queue — 2026-10-20, round b

| order | PR | head | row | gate | verdict | state | next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | this round PR | pending | board/log/Phase D | pending | script | LANDING | land |
| 2 | #422 | `38cdb9f` | #163 | pending | — | VERIFY | verify1 |
| 3 | #423 | `8da0451` | #165 | pending | — | VERIFY | verify2 (script half) |
| 4 | #164 | pending | #164 | pending | — | IN FLIGHT | build2 |
| 5 | #169 | pending | #169 | pending | — | IN FLIGHT | recon2 (Phase D seed 1) |
| 6 | #170 | pending | #170 | pending | — | IN FLIGHT | build3 (Phase D seed 2) |

**No auto-merge is armed.** Landed this round: #414, #417, #416, #418, #421, #420,
#419.

## Held / not landing yet

- **#171** (raw capture) — depends #161 and #132; not dispatched.
- **#172** (RPC shim) — depends #169, #170, #124, #131; must not start before #169.
- **The product switch (#100)** — held behind #169-#172 and #126. No product switch
  this round.
- **P12's template half (#165)** — `tests/results/_TEMPLATE.md` is denied at the tool
  level for both the builder and the dispatcher. The operator must apply the two-line
  requirement or grant the path; the row stays `SPEC-READY` until then.

## Landed this round (2026-10-20b)

| PR | merge | what |
| --- | --- | --- |
| #414 | `b9e7dc7` | #161 depth-hang finding (finding-only) |
| #417 | `ae06afd` | handoff/board/queue/test-queue pass |
| #416 | `aef16f2` | #161 bounded live RPC timeout |
| #418 | `633e660` | #167 device-enumeration retry |
| #421 | `a85d811` | #168 capture-path bound / fail-closed |
| #420 | `d1c0e42` | 2026-10-20 record batch + board corrections |
| #419 | `4059c21` | #162 P9 flash-write falsifier |

## Merge order

1. This round PR (script-tier).
2. #422 and #423 as their verdicts land.
3. #164, then the Phase D seeds (#169/#170) as their PRs open.
4. Next: #149, #154, #103, #118, #116.

A PR is added here when it opens; it leaves when merged or closed.
