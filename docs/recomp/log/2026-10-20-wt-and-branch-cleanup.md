# 2026-10-20 — worktree and branch cleanup (addendum to the salvage audit)

Dispatcher session: `ses_ea7857cafffeHu7Fnrmx1bQFxj`.

## Worktrees

The layout now matches the roster exactly: **12 worktrees, 0 legacy**.

| slot | path | base |
| --- | --- | --- |
| dispatch | `~/DevReposiMac/Open3DScanner-wt-dispatch` | `dispatch/next` |
| plan | `~/DevReposiMac/Open3DScanner` | `planner/next` |
| recon1 | `~/DevReposiMac/Open3DScanner-wt-recon1` | `recon1/next` |
| recon2 | `~/DevReposiMac/Open3DScanner-wt-recon2` | `recon2/next` |
| build1 | `~/DevReposiMac/Open3DScanner-wt-build1` | `build1/next` |
| build2 | `~/DevReposiMac/Open3DScanner-wt-build2` | `build2/next` |
| build3 | `~/DevReposiMac/Open3DScanner-wt-build3` | `build3/next` |
| verify1 | `~/DevReposiMac/Open3DScanner-wt-verify1` | `verify1/next` |
| verify2 | `~/DevReposiMac/Open3DScanner-wt-verify2` | `verify2/next` |
| verify3 | `~/DevReposiMac/Open3DScanner-wt-verify3` | `verify3/next` |
| verify4 | `~/DevReposiMac/Open3DScanner-wt-verify4` | `verify4/next` |
| hw | `~/DevReposiMac/Open3DScanner-wt-hw` | `hw/next` (user-owned, dirty) |

Removed: the 25 pre-roster rooms (`-wt-analysis`, `-wt-board`, `-wt-build`,
`-wt-build4..11`, `-wt-landhw2`, `-wt-recon`, `-wt-recon3..6`, `-wt-verify`,
`-wt-verify5..7`, and the four OpenCode-managed worktrees under
`~/.local/share/opencode/worktree/9b6b53/`). `-wt-hw` was not touched.

Dirty drafts preserved before removal, then deleted after the audit (SHAs for
recovery):

- `recon2/next-legacy-2026-10-20` = `573f391`
- `build2/next-legacy-2026-10-20` = `4906163`
- `planner/next-legacy-2026-10-20` = pre-reset `planner/next`
- `salvage/recon4-row114-2026-10-20` = `d892a6c`
- `salvage/recon6-row144-2026-10-20` = `bdd8570`
- `salvage/verify4-raptor-2026-10-20` = `178fa9e`

Their content is in `main` under the landed revisions (#402/#404/#405), or
superseded; the audit is `2026-10-20-salvage-audit.md`.

## GitHub branch auto-delete

`delete_branch_on_merge` is **true** on `juicie-brucie/Open3DScanner`. The
#404, #405 and #406 merges each exercised it: their remote heads were deleted
automatically.

## Final branch set

**14 local, 14 remote**: the 12 roster bases, `main`, and the open **#392**
head. Nothing else.

## Landed during the salvage/cleanup

| PR | merge | what |
| --- | --- | --- |
| #399 | `e1c5e2b` | row #93 fix (verify1 `MERGEABLE @ 92cc326`) |
| #400 | `ff89839` | `wt-roster.py` + `pr-rebase.py` + queue (verify2 `MERGEABLE @ 91e6514`, after the re-read) |
| #401 | `4d47fe0` | roster namespace fix (verify3 `MERGEABLE @ 85c676e`) |
| #402 | `83f9e27` | planner revs 20-24 (script-tier) |
| #403 | `3add5e9` | #93 → HW, queue, salvage audit (script-tier) |
| #404 | `807a9f1` | planner rev 25 (script-tier) |
| #406 | `3382c87` | planner rev 25 correction, recovered from the unpushed `82c865d` (script-tier) |
| #405 | `c2d6d6d` | row #144 salvage (verify2 `MERGEABLE @ f3ab589` after the three fixes and the spacing nit) |

## Corrections to the salvage audit

- **The FORK-COORDINATION stray-verdict entry is already in `main`.** The
  `dispatch/land-stray` branch made it look unlanded; `git grep` on
  `origin/main` finds it. No extra commit was needed on #404.
- **`planner/25-single-session` carried one unpushed correction after #404's
  head** (`82c865d`). GitHub's auto-delete removed the remote head on merge;
  the local branch was the only copy. Recovered and landed as **#406**.
- The row #144 deliverable was capped at the recon step budget but had already
  produced #405; its local gate failure was the new worktree's uninitialized
  submodules, not the docs. CI `verify-pr` was SUCCESS.

## Open

- **#392** is the only open PR: a draft HOLD failing `archives` and
  `record-provenance`, 59 commits behind. It needs the hardware tester /
  operator's decision to repair or close.
- The recon agent file still pins `qwen3.7-plus#medium`, which the provider no
  longer offers (`none`/`high`/`max`). The recon1 replacement ran `#high`; the
  agent file needs a one-line correction by its owner.
