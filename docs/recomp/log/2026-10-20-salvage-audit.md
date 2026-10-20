# 2026-10-20 — salvage audit: unlanded commits and left-behind work

Dispatcher session: `ses_ea7857cafffeHu7Fnrmx1bQFxj` (`Open3DScanner-wt-dispatch`).

Motivated by the two legacy bases found during adoption: `recon2/next` carried
9 commits and `build2/next` 1, both never landed as commits. This audit
classifies every ref with commits outside `origin/main`, the dirty rooms, the
stashes, and the recent dangling commits.

## Method

- `git rev-list --count origin/main..<ref>` for every local branch (59 had
  unique commits).
- `git diff <merge-base>..<ref>` for the branch's own changes; board/log churn
  (`PLAN.md`, `docs/recomp/log/**`, `ROADMAP.md`) was excluded from the
  code-content test.
- For each changed non-board file, the branch's blob was looked up in
  `origin/main`'s history (`git log --find-object`). A blob never in main's
  history is a genuine content difference; a blob in history was landed
  (possibly later superseded).
- `git fsck --unreachable --no-reflogs` for dangling commits; `git stash list`;
  `git status` in every worktree.

## What was genuinely unlanded

1. **Row #144 — the 208-B segments and the op-31 structures.** A complete
   deliverable (354-line `small-segments-row144.md` plus corrections to
   `protocol-spec.md` and `stream-profiles-row114.md`) sat **uncommitted** in
   the legacy `-wt-recon6` on `recon/row144-p5`, 102 commits behind main. It
   answers both halves of the row and closes `#114`'s OQ6. Salvaged into
   `recon1/144-p5`; a recon session verifies three measurements against the
   existing capture and opens the PR.
2. **The planner's ROADMAP revisions.** `planner/next` carried 8 commits
   (revs 20, 21, 23, 24, then rev 25 written during this audit) with a
   626-insertion / 160-deletion `ROADMAP.md` diff and no PR. The planner is
   landing them on `planner/24-roadmap`.
3. **A 12-line `FORK-COORDINATION.md` entry** recording the 2026-10-19
   stray verdict (the fork lane posted a verdict on the parent's PR #11 by
   mistake; inert for the gate, comment deleted). It existed only on
   `dispatch/land-stray`. Folded into the planner's PR.

## What looked unlanded but is not

- **`recon2/next`'s 9 commits** — `git cherry` showed 7 unapplied patches, but
  every changed non-board file's blob is in main's history; the recon content
  (rows #101, #102, #105, #261, #268) landed through later PRs. The commits
  are preserved as `recon2/next-legacy-2026-10-20` locally **and on `origin`**.
- **`build2/next`'s 1 commit (row #91)** — main carries `tracking_status.h`,
  `tracking_status_test.cpp` and the `main.cpp` wiring. The commit is preserved
  as `build2/next-legacy-2026-10-20` locally and on `origin`, but it is a
  duplicate.
- **`recon4`'s dirty work (row #114)** — the new `stream-profiles-row114.md`
  and the `flash-writes-row102.md` / `protocol-spec.md` corrections are all in
  main under later revisions. The patch is saved at
  `/private/var/folders/.../opencode/salvage/recon4.diff`; the worktree is
  untouched.
- **`verify4`'s staged `raptor_client` change** — the same features
  (`--dry-run-live`, `--pull-ms`, `VendorAccepted`, `--dump-replies`,
  `--capture`) are in main. The 127-line staged diff is an intermediate; patch
  saved at `.../salvage/verify4-staged.diff`. The slot stays dirty and is not
  reused.
- **The `#151` raw-log correction** (`b4f9085`) and the **`#147` 24.29 ms
  correction** (`ab3f730`) are both in main under their landed versions.
- **`build/plan120`, `pr303*`, `tmp-303*`** — the `tests/results` routing
  record exists in main; the branches are rebuilds of the same work.
- **`pr359*`, `tmp5-*`, `pr366`, `tmp-build-plan140/141`** — main carries the
  frame-age, `perf_window`, `capture.cpp/.h` and tracking implementations; the
  branches are intermediate rebuilds.
- **No log entry is missing**: every `docs/recomp/log/*.md` referenced by an
  unlanded branch is present in main; the branches modify existing entries,
  they do not add lost ones.
- **No stash** exists.
- **No recent dangling commit is unique**: the 20 inspected are pre-merge or
  pre-rebase duplicates of landed commits; `git fsck` shows no lost branch tip
  other than the ones preserved above.

## Dirty rooms (reported, not touched)

| room | state | what it is |
| --- | --- | --- |
| `-wt-recon6` | dirty, 4 files | row #144 draft — copied to `recon1`; the original is left in place |
| `-wt-recon4` | dirty, 3 files | row #114 draft — superseded by main; patch saved |
| `-wt-verify4` | dirty, 1 staged file | raptor_client intermediate — superseded; patch saved |
| `-wt-hw` | dirty, 10 untracked | the 2026-10-20 hardware records (`#392`'s content); user-owned |

## Preserved refs

- `recon2/next-legacy-2026-10-20` = `573f391` (local + `origin`)
- `build2/next-legacy-2026-10-20` = `4906163` (local + `origin`)

The roster bases `recon2/next` and `build2/next` are clean cuts of
`origin/main`.

## Open

- The recon agent file pins `qwen3.7-plus#medium`, which the provider no longer
  offers (`none` / `high` / `max` only). The replacement recon session runs
  `#high`; the agent file needs a one-line correction by its owner.
- 55 local branches remain in the `decide` bucket; all inspected content is
  landed or superseded, but they are left for the user's call.
