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
  delete the branch locally, and let GitHub delete the remote head.
- **WIP cap:** no new build while open agent-tier PRs exceed the four verify
  slots.

States: `LANDING` (auto-merge armed) · `VERIFY` (gate/verdict pending) ·
`QUEUED` (verdict at head, waiting for the slot) · `FIX` (NEEDS CHANGE) ·
`HOLD` (not mergeable for a non-code reason) · `IN FLIGHT` (the WO is running).

## Queue — 2026-10-20, after #392/#412

| order | PR | head sha | board row | gate | verdict @ head | state | next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | #413 | `cbf3037` | #160 | running | none | VERIFY | build1 settles; then verify |
| 2 | #414 | `51f028f` | #161 | pass | none | VERIFY | recon1 settles; then verify |
| — | P9–P12 / #166 | — | #162–#166 | — | — | QUEUED | dispatch after #413/#414 |

**No auto-merge is armed on any open PR.** #413 and #414 touch disjoint files
(`ui/**` and `docs/recon/**`); neither touches the board. Whichever gets a
verdict first takes the single landing slot; the other waits with its verdict.

## Landed this round (recent)

| PR | merge | verdict |
| --- | --- | --- |
| #412 | `87345fe` | script-tier (board/trust pass) |
| #392 | `969cb1a` | `MERGEABLE @ e3f2727` (verify1; records repair) |
| #409 | `f3c32ff` | script-tier (test queue) |
| #410 | `3bfd8b5` | `MERGEABLE @ ba5ce6e` (verify1; #76 frozen) |
| #411 | `b13da57` | `MERGEABLE @ 882e7ad` (verify2; #127 retry) |

## Merge order

1. **#413 / #414** — whichever gets a verdict first; exactly one armed; the
   other waits.
2. **P9–P12 / #166** — dispatched after the current two are drained.

A PR is added here when it opens; it leaves when it is merged or closed. The
dispatcher updates this file in the same turn as the state change.
