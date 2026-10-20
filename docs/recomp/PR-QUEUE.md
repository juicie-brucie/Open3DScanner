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
| 1 | #144 salvage | pending | #144 | pending | — | QUEUED | recon1 opens it; verify at head |
| 2 | planner rev 25 | pending | — | pending | — | QUEUED | planner opens it; verify at head |
| — | #392 | `02e0870a0b15bcd9c4feb5bfd20b50aed4bddc93` | HW records | FAIL | none | HOLD | draft; fix `verify-pr` before it enters the queue |

## Landed this round

| PR | head sha | verdict | merge commit | landed |
| --- | --- | --- | --- | --- |
| #399 | `92cc3264c785c44f0c82c1ae7c9af71be42929c0` | `MERGEABLE @ 92cc326` (verify1) | `e1c5e2b4e991bf5caa1b831b4ad57a5e9db6645b` | 2026-10-20T08:59:30Z |
| #400 | `91e6514f0267eb7fd1d1ea2568cab1c984dd2b72` | `MERGEABLE @ 91e6514` (verify2, after the re-read) | `ff898398baad2347577563cc68fabaff792346d1` | 2026-10-20T09:14:42Z |
| #401 | `85c676e5645ad6a035efaa0c50d3a15680b8db4f` | `MERGEABLE @ 85c676e` (verify3) | `4d47fe007f2d9a649ae8d8f3b90065299c06e517` | 2026-10-20T09:22:33Z |
| #402 | `51ec9000bd71b5a411a01e20b9e787f2e8c263ad` | script-tier (auto-verdict) | `83f9e273a691dbea755e20c6f66f5000e5d723ae` | 2026-10-20T09:24:28Z |

## Merge order

1. The #144 salvage PR — the completed recon deliverable; it also carries the
   `TODO` -> `SPEC-READY` row move.
2. The planner's rev 25 PR — the single-session revision plus the stray-verdict
   entry.
3. `#392` — not in the queue while it is a draft and failing its gate.

A PR is added here when it opens; it leaves when it is merged or closed. The
dispatcher updates this file in the same turn as the state change.
