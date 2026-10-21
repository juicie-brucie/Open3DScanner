# PR landing queue

Owner: dispatch. One landing in flight; the GitHub merge queue is not used.

## Queue — 2026-10-20, handoff

| order | PR | head | row | gate | verdict | state | next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | #414 | `66f3990` | #161 | pass; scope fix running | `NEEDS CHANGE` (scope only) | FIX | recon1 restores `PLAN.md`; verify2 re-read; arm |
| 2 | handoff/board | this PR | #160/#161/#167/#168 | pending | — | LANDING | land **after #414** |
| 3 | #161 build | pending | #161 | pending | — | IN FLIGHT | build3 opens the timeout PR |
| — | #167 | — | #167 | — | — | QUEUED | the device-enumeration race fix |
| — | #168 | — | #168 | — | — | QUEUED | the `--capture` path fix |
| — | records | — | #151/#71/#93/#132/#127 | — | — | QUEUED | provenance/P12 |
| — | P9–P12 / #166 | — | #162–#166 | — | — | QUEUED | after the blockers |

**No auto-merge is armed.** #414 is the only open agent-tier PR; the
handoff/board PR is script-tier and waits behind it so #414 merges
finding-only.

## Landed this round (recent)

| PR | merge | what |
| --- | --- | --- |
| #413 | `8d377fe` | #160 coverage update fix |
| #415 | `813adfe` | queue refresh |
| #412 | `87345fe` | board/trust pass |
| #392 | `969cb1a` | 2026-10-20 records repair |
| #409 | `f3c32ff` | hardware test queue |
| #410 | `3bfd8b5` | #76 frozen |
| #411 | `b13da57` | #127 live retry |

## Merge order

1. **#414** — the depth finding, finding-only; re-read, arm, merge.
2. **This PR** — the handoff/board/queue/test-queue update.
3. **#161 build** — verify and land; the depth retry card.
4. **#167** (the device race) and **#168** (the capture path) — the new blockers.
5. The unlanded records (provenance/P12).
6. P9–P12 / #166.

A PR is added here when it opens; it leaves when merged or closed.
