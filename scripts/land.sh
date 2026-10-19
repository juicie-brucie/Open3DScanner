#!/usr/bin/env bash
# Start a landing branch the safe way.
#
# Why this exists: a landing cut from a stale base (a branch tip that predates
# the rows it means to move) had its precondition fail silently inside a longer
# shell command, and the PR went up without its payload. This script fails fast,
# cuts the branch deliberately, and ASSERTS its preconditions before anything
# can be committed.
#
# Usage:
#   scripts/land.sh <branch> [--base <ref>] [--require <path>:<pattern>]...
#     --base      default origin/main. Anything else is printed loudly together
#                 with the commits it carries - a landing off an unlanded branch
#                 is intended for recon/build landings, not for board moves.
#     --require   a precondition asserted AFTER the branch is cut, e.g.
#                 --require docs/recomp/PLAN.md:'| 70 |'
#                 Repeatable. A failed assertion exits 3 and leaves the branch
#                 for inspection - nothing is committed.
# Exit: 0 ready · 2 refused (dirty tree, bad args, branch exists)
#       3 precondition failed
set -euo pipefail

branch="${1:?usage: land.sh <branch> [--base <ref>] [--require <path>:<pattern>]...}"; shift
base="origin/main"; reqs=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --base)    base="${2:?--base needs a value}"; shift 2 ;;
    --require) reqs+=("${2:?--require needs <path>:<pattern>}"); shift 2 ;;
    *) echo "land.sh: unknown argument: $1" >&2; exit 2 ;;
  esac
done

root=$(git rev-parse --show-toplevel)
cd "$root"
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
  echo "land.sh: uncommitted tracked changes - refusing to cut a landing branch" >&2
  exit 2
fi
git fetch -q origin 2>/dev/null || true
git rev-parse --verify -q "$base" >/dev/null || { echo "land.sh: base not found: $base" >&2; exit 2; }
if git rev-parse --verify -q "refs/heads/$branch" >/dev/null; then
  echo "land.sh: branch already exists: $branch" >&2; exit 2
fi

git switch -q -c "$branch" "$base"
echo "land.sh: $branch cut from $base at $(git rev-parse --short HEAD)"
if [ "$base" != "origin/main" ]; then
  echo "land.sh: NOTE base is not origin/main; this landing carries its commits:"
  git log --oneline "origin/main..HEAD" | head -5 | sed 's/^/    /'
  echo "land.sh: and it sits $(git rev-list --count "HEAD..origin/main") commit(s) behind main"
fi

n=0
for r in ${reqs[@]+"${reqs[@]}"}; do
  n=$((n + 1))
  path="${r%%:*}"; pat="${r#*:}"
  if [ ! -f "$path" ]; then
    echo "land.sh: PRECONDITION $n FAILED - no such file: $path" >&2; exit 3
  fi
  if ! grep -q -F -- "$pat" "$path"; then
    echo "land.sh: PRECONDITION $n FAILED - '$pat' not found in $path" >&2
    echo "land.sh: the base is stale for this landing (nothing was committed; the branch is left for inspection)" >&2
    exit 3
  fi
  echo "land.sh: precondition $n ok - '$pat' in $path"
done

echo "land.sh: ready. Apply the edits, then commit."
echo "land.sh: keep the rest of the command fail-fast (set -euo pipefail at the top) so a failed step cannot be committed over."
