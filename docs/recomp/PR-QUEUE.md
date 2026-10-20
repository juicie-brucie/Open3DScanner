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
| 1 | #400 | `32738de004473bde0c4147919bf2ee58d7231a83` | — | PASS | none | VERIFY | verify2 posts the anchored verdict |
| — | #392 | `02e0870a0b15bcd9c4feb5bfd20b50aed4bddc93` | HW records | FAIL | none | HOLD | draft; fix `verify-pr` before it enters the queue |

## Landed this round

| PR | head sha | verdict | merge commit | landed |
| --- | --- | --- | --- | --- |
| #399 | `92cc3264c785c44f0c82c1ae7c9af71be42929c0` | `MERGEABLE @ 92cc326` (verify1) | `e1c5e2b4e991bf5caa1b831b4ad57a5e9db6645b` | 2026-10-20T08:59:30Z |

## Merge order

1. `#400` — gate green; verify at its head, then arm auto-merge.
2. `#392` — not in the queue while it is a draft and failing its gate.

A PR is added here when it opens; it leaves when it is merged or closed. The
dispatcher updates this file in the same turn as the state change.
