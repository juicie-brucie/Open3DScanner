# 2026-10-20 — roster adoption, branch prune, and the #93 test work order

Dispatcher session: `ses_ea7857cafffeHu7Fnrmx1bQFxj` (moved from the plan room
to `Open3DScanner-wt-dispatch` before any write).

## Swarm line

dispatch → `ses_ea7857cafffeHu7Fnrmx1bQFxj` · plan → `ses_ea7b26829ffex4zW2UapZWhbuX` ·
recon1 → `ses_ea78172dcffeccNC4thZqcru0h` · recon2 → `ses_ea7817223ffe5e3UU3OEy88pMZ` ·
build1 → `ses_ea7817175ffe40vyuvwNLc77RR` · build2 → `ses_ea7ae25f1ffeW7Wai8sLU6Z8Mv` ·
build3 → `ses_eaa77bd4effeynzJ7pzx8kkpDH` · verify1 → `ses_ea78170c7ffeQl2yUx4lVPOveM` ·
verify2 → `ses_ea7817014ffeqRw5157SNi10d1` · verify3 → `ses_ea7816f64ffeAaBIRE1BIy2YFZ`.

## Branch prune (GitHub)

- 194 remote branches classified; **170 ancestors of `origin/main` deleted**,
  0 failures.
- 24 remote branches kept: 4 protected (main, the roster bases touched, the
  open PR head) and 20 checked out in a worktree / under a dirty slot.
- 5 remote branches remain unmerged and are reported, never deleted:
  `build/plan105b-replay`, `build/plan117-replay-pacing`,
  `dispatch/fix-row138-title`, `docs/row-147-review-fixes`,
  `build/plan108-pipeline-status` (checked out in `-wt-build`).
- Local: 209 ancestors are deletable; 25 unclassified are report-only pending
  the user's decision. No local branch was deleted this round.

## Worktree adoption (one-time)

The fixed 12-row roster is now real at the exact paths:

- created `-wt-recon1` (`recon1/next`), `-wt-build1` (`build1/next`),
  `-wt-verify1..3` (`verify*/next`), all cut from `origin/main`;
- returned `-wt-recon2`, `-wt-build2`, `-wt-build3` to their bases;
- `recon2/next` carried 9 unlanded commits and `build2/next` 1; both were
  preserved as `recon2/next-legacy-2026-10-20` /
  `build2/next-legacy-2026-10-20`, and clean bases were cut from `origin/main`;
- `plan` remains the main room on `planner/next`; `hw` remains the user's
  Claude Code room (dirty, untouched);
- one session per slot; the ten OpenCode slots are listed in the swarm line.

`scripts/wt-roster.py check` now passes: 12 slots, 25 legacy rooms reported
(never bound), 3 running sessions at the time of the run. Warnings:
`-wt-dispatch` was dirty with this tooling while the check ran; `-wt-verify4`
is dirty and unbound (not reused until clean).

## PR queue

- `#399` (`build1/93-tracking-debounce`, head `92cc326`) — `verify-pr` PASS,
  no verdict at head → sent to verify1.
- `#392` (HOLD: hardware records) — draft, `verify-pr` FAIL → not in the queue.
- The tooling PR (`scripts/wt-roster.py`, `scripts/pr-rebase.py`, this entry,
  `PR-QUEUE.md`) — script-tier, `QUEUED`.

## Test work order — #93

`build1 (Build #93 — tracking-line double-debounce)` was dispatched one board
row at a time. It removed `effectiveTracking()`/`markerlessRun` from
`ui/app.js` and opened `#399` at `92cc326`; the local gate is green. The
verifier is next; the row is recommended `BUILD` → `HW` because the markerless
rendering observation needs the scanner.

## Open

- `-wt-verify4` is dirty (one file) and unbound; do not reuse until clean.
- `-wt-hw` is dirty; user-owned, untouched.
- 25 legacy rooms remain; the user drains them.
- 25 local branches await the user's classification.
