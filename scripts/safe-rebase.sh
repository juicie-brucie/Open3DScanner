#!/usr/bin/env bash
# Rebase a room onto a target and NEVER leave a conflicted rebase behind.
#
# Why this exists: an ad-hoc `git rebase` inside a longer shell command that
# keeps going after the conflict leaves the repo mid-rebase, and a following
# `git commit --amend` bakes conflict markers into a commit (that happened once;
# see docs/recomp/log). This script aborts on the first conflict, restores the
# branch, and reports. On success it verifies no conflict markers came along.
#
# Usage: scripts/safe-rebase.sh <target-ref> [room-path]
#   target-ref   e.g. origin/main
#   room-path    default: this repository's top level
# Exit: 0 rebased (or already current) · 1 conflict (aborted, branch unchanged)
#       2 refused (dirty tree, rebase already in progress, bad args)
set -euo pipefail

target="${1:?usage: safe-rebase.sh <target-ref> [room-path]}"
room="${2:-}"
[ -n "$room" ] || room=$(git rev-parse --show-toplevel)
if [ ! -d "$room/.git" ] && [ ! -f "$room/.git" ]; then
  echo "safe-rebase: not a worktree: $room" >&2; exit 2
fi

if [ -n "$(git -C "$room" status --porcelain --untracked-files=no)" ]; then
  echo "safe-rebase: $room has uncommitted tracked changes - refusing (commit or stash first)" >&2
  exit 2
fi
# Resolve the git dir properly: in a linked worktree $room/.git is a FILE, so
# testing "$room/.git/rebase-merge" would silently never fire - and a pending
# rebase would then be misread as our own conflict and aborted (the exact
# cross-session abort this script exists to prevent).
gitdir=$(git -C "$room" rev-parse --absolute-git-dir 2>/dev/null || true)
if [ -n "$gitdir" ] && { [ -d "$gitdir/rebase-merge" ] || [ -d "$gitdir/rebase-apply" ]; }; then
  echo "safe-rebase: $room already has a rebase in progress - refusing (finish or abort it first)" >&2
  exit 2
fi

git -C "$room" fetch -q origin 2>/dev/null || true
git -C "$room" rev-parse --verify -q "$target" >/dev/null || {
  echo "safe-rebase: unknown target: $target" >&2; exit 2; }

before=$(git -C "$room" rev-parse HEAD)
err=$(mktemp -t safe-rebase.XXXXXX)
if ! git -C "$room" rebase "$target" 2>"$err"; then
  echo "safe-rebase: conflict rebasing onto $target - aborting" >&2
  grep -E 'CONFLICT|^error:' "$err" | sed -n '1,10p' >&2 || true
  if [ -d "$gitdir/rebase-merge" ] || [ -d "$gitdir/rebase-apply" ]; then
    git -C "$room" rebase --abort >/dev/null 2>&1 || true
  fi
  rm -f "$err"
  now=$(git -C "$room" rev-parse HEAD)
  if [ "$now" = "$before" ]; then
    echo "safe-rebase: branch restored at $(git -C "$room" rev-parse --short HEAD); nothing was pushed" >&2
    echo "safe-rebase: resolve the overlap deliberately (merge, or edit then re-run) - do not blind-continue a rebase" >&2
  else
    echo "safe-rebase: WARNING branch is now $(git -C "$room" rev-parse --short HEAD) (was $(git -C "$room" rev-parse --short "$before")) - inspect before pushing" >&2
  fi
  exit 1
fi
rm -f "$err"

if git -C "$room" grep -n -E '^(<<<<<<< |>>>>>>> )' -- . >/dev/null 2>&1; then
  echo "safe-rebase: the rebase completed but left conflict markers in the tree - NOT pushing; inspect the branch" >&2
  exit 1
fi

after=$(git -C "$room" rev-parse HEAD)
if [ "$before" = "$after" ]; then
  echo "safe-rebase: already current with $target ($(git -C "$room" rev-parse --short HEAD))"
else
  echo "safe-rebase: rebased $(git -C "$room" rev-parse --short "$before") -> $(git -C "$room" rev-parse --short "$after") onto $target"
  echo "safe-rebase: if the branch was pushed, publish with: git push --force-with-lease origin HEAD"
fi
