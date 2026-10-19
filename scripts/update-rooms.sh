#!/usr/bin/env bash
# Rooms stay current when idle (charter §4): a room behind `origin/main` is
# missing merged PRs, and its next task starts from a stale tree.
#
# For every worktree of this repository (or the room paths given):
#   dirty tracked files            -> SKIP (uncommitted work is never touched)
#   branch is an ancestor of main  -> fast-forward
#   every commit already landed    -> rebase onto origin/main + lease-push
#                                     (only when `git cherry` shows all '-')
#   anything else (unlanded work)  -> SKIP - its author's business
#   detached HEAD                  -> reported, never moved
#
# The CALLER decides when it is safe: run it only while the sessions in those
# rooms are not mid-turn. The script's guarantees are about *work* - it will
# not discard a change or rebase a branch carrying unlanded commits.
#
# Usage: scripts/update-rooms.sh [--dry-run] [--self] [room-path ...]
#   --self      update only THIS worktree - the safe form for an agent to run
#               as the first step of a work order (it cannot touch another
#               session's room)
#   room-path   update only the named room(s)
#   no paths    every worktree of this repository (the dispatcher's form)
set -euo pipefail

DRY=""; SELF=""
while [ "${1:-}" = "--dry-run" ] || [ "${1:-}" = "--self" ]; do
  if [ "$1" = "--dry-run" ]; then DRY=1; else SELF=1; fi
  shift
done

MAIN=$(git rev-parse --show-toplevel)
git -C "$MAIN" fetch -q origin
BASE=$(git -C "$MAIN" rev-parse origin/main)
SHORT=$(git -C "$MAIN" rev-parse --short "$BASE")

if [ -n "$SELF" ]; then
  ROOMS=("$MAIN")
elif [ "$#" -gt 0 ]; then
  ROOMS=("$@")
else
  ROOMS=()
  while IFS= read -r p; do ROOMS+=("$p"); done < <(git -C "$MAIN" worktree list --porcelain | awk '/^worktree /{print $2}')
fi

for room in "${ROOMS[@]}"; do
  name=${room#"$HOME"/}
  if ! git -C "$room" rev-parse --git-dir >/dev/null 2>&1; then
    printf '%-44s %s\n' "$name" "not a repo - skipped"
    continue
  fi
  branch=$(git -C "$room" symbolic-ref --short -q HEAD || true)
  if [ -z "$branch" ]; then
    printf '%-44s %-20s %s\n' "$name" "(detached)" "reported, not moved"
    continue
  fi
  dirty=$(git -C "$room" status --porcelain --untracked-files=no | wc -l | tr -d ' ')
  if [ "$dirty" != "0" ]; then
    printf '%-44s %-20s SKIP: %s uncommitted path(s)\n' "$name" "$branch" "$dirty"
    continue
  fi
  if git -C "$room" merge-base --is-ancestor HEAD "$BASE"; then
    if [ -n "$DRY" ]; then
      act="would fast-forward to $SHORT"
    else
      git -C "$room" merge --ff-only "$BASE" >/dev/null 2>&1
      act="fast-forwarded to $(git -C "$room" rev-parse --short HEAD)"
    fi
  elif [ -z "$(git -C "$room" cherry "$BASE" HEAD | grep -v '^-' || true)" ]; then
    n=$(git -C "$room" rev-list --count "$BASE"..HEAD)
    if [ -n "$DRY" ]; then
      act="would rebase $n landed commit(s) + lease-push"
    else
      git -C "$room" rebase "$BASE" >/dev/null 2>&1
      if git -C "$MAIN" ls-remote --exit-code --heads origin "$branch" >/dev/null 2>&1; then
        git -C "$room" push --force-with-lease origin "$branch" >/dev/null 2>&1
        act="rebased $n landed commit(s) + lease-pushed"
      else
        act="rebased $n landed commit(s) (no remote branch)"
      fi
    fi
  else
    n=$(git -C "$room" cherry "$BASE" HEAD | grep -v '^-' | wc -l | tr -d ' ')
    act="SKIP: $n unlanded commit(s)"
  fi
  printf '%-44s %-20s %s\n' "$name" "$branch" "$act"
done
