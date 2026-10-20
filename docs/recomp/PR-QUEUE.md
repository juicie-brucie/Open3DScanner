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
  then delete the branch locally and on `origin`.
- **WIP cap:** no new build while open agent-tier PRs exceed the four verify
  slots. Drain first.

States: `LANDING` (auto-merge armed) · `VERIFY` (waiting on a verdict) ·
`QUEUED` (verdict at head, waiting for the slot) · `FIX` (NEEDS CHANGE) ·
`HOLD` (not mergeable for a non-code reason).

## Queue — 2026-10-20

| order | PR | head sha | board row | verify-pr | verdict @ head | state | next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | #399 | `92cc3264c785c44f0c82c1ae7c9af71be42929c0` | #93 | PASS | none | VERIFY | verify1 posts the anchored verdict |
| — | #392 | `02e0870a0b15bcd9c4feb5bfd20b50aed4bddc93` | HW records | FAIL | none | HOLD | draft; fix `verify-pr` before it enters the queue |
| — | tooling PR | pending | — | pending | auto (script-tier) | QUEUED | open, then arm auto-merge |

## Merge order

1. `#399` — the only open agent-tier PR; verify at its head, then arm
   auto-merge.
2. The tooling PR — script-tier; it lands independently once `verify-pr` is
   green.
3. `#392` — not in the queue while it is a draft and failing its gate.

A PR is added here when it opens; it leaves when it is merged or closed. The
dispatcher updates this file in the same turn as the state change.
