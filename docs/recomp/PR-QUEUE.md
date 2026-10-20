# PR landing queue

Owner: dispatch. This is the single landing queue; GitHub's merge queue is not
used because it would test a queue head no verifier has read.

## Rules

- **One landing in flight.** Only the top row has auto-merge armed. Every other
  ready PR waits with its verdict intact.
- **Merge the verified head.** Do not rebase for freshness; a rebase moves the
  head and voids the verdict. Only a conflict justifies a rebase.
- **A verdict expires when the head moves.** Re-take it; never carry it across.
- **Never merge a `NEEDS CHANGE`.**
- **A branch name is single-use.** After a merge: return the slot to its base,
  then delete the branch locally and on `origin` (GitHub does the remote half).
- **WIP cap:** no new build while open agent-tier PRs exceed the four verify
  slots.

States: `LANDING` (auto-merge armed) · `VERIFY` (waiting on a verdict) ·
`QUEUED` (verdict at head, waiting for the slot) · `FIX` (NEEDS CHANGE) ·
`HOLD` (not mergeable for a non-code reason) · `IN FLIGHT` (the WO is running).

## Queue — 2026-10-20 evening

| order | PR | head sha | board row | gate | verdict @ head | state | next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | board/trust pass | pending | #134/#155/#138 closed; #162-#166 filed | pending | — | LANDING | verify at head; then merge |
| 2 | #160 | pending | #160 | pending | — | IN FLIGHT | build1 opens the coverage fix |
| 3 | #161 | pending | #161 | pending | — | IN FLIGHT | recon1 opens the depth-hang finding |
| — | #392 | `02e0870` | HW records | FAIL | none | HOLD | repair the three records, rebase, undraft, re-gate, verify |
| — | P9-P12 / #166 | — | #162-#166 | — | — | QUEUED | dispatch after the current three |

## Landed this round (recent)

| PR | merge | verdict |
| --- | --- | --- |
| #399 | `e1c5e2b` | `MERGEABLE @ 92cc326` (verify1) |
| #400 | `ff89839` | `MERGEABLE @ 91e6514` (verify2) |
| #401 | `4d47fe0` | `MERGEABLE @ 85c676e` (verify3) |
| #402 | `83f9e27` | script-tier |
| #403 | `3add5e9` | script-tier |
| #404 | `807a9f1` | script-tier |
| #405 | `c2d6d6d` | `MERGEABLE @ f3ab589` (verify2) |
| #406 | `3382c87` | script-tier |
| #407 | `fc6a081` | script-tier |
| #408 | `003dd74` | `MERGEABLE @ df6c6dd` (verify3) |
| #409 | `f3c32ff` | script-tier (test queue) |
| #410 | `3bfd8b5` | `MERGEABLE @ ba5ce6e` (verify1) |
| #411 | `b13da57` | `MERGEABLE @ 882e7ad` (verify2) |

## Merge order

1. The board/trust pass (this PR) — docs/board only.
2. **#160** and **#161** when their PRs open; verify at each head.
3. **#392** after the record repairs: rebase, undraft, re-gate, verify.
4. P9-P12 / #166 after the current three are drained.

A PR is added here when it opens; it leaves when it is merged or closed.
