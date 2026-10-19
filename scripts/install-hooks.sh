#!/bin/sh
# Install this repo's versioned git hooks (row #110).
#
# Instead of copying into .git/hooks (which is not versioned), this points
# `core.hooksPath` at the checked-in `scripts/git-hooks` directory. A relative
# path resolves against each worktree's top level, so every room of this repo
# gets the hook from its own checkout — no per-room copy, nothing to drift.
#
# Idempotent: re-running is a no-op that reports the current state.
# Refuses to replace a different `core.hooksPath`, or to bypass an existing
# .git/hooks/pre-commit, without --force.
set -eu

want="scripts/git-hooks"
force=0
for arg in "$@"; do
    case "$arg" in
        --force) force=1 ;;
        -h|--help) echo "usage: scripts/install-hooks.sh [--force]"; exit 0 ;;
        *) echo "install-hooks: unknown argument: $arg" >&2; exit 2 ;;
    esac
done

root=$(git rev-parse --show-toplevel 2>/dev/null) || {
    echo "install-hooks: not inside a git worktree" >&2
    exit 2
}

hook="$root/$want/pre-commit"
if [ ! -f "$hook" ]; then
    echo "install-hooks: missing $hook" >&2
    exit 2
fi

current=$(git config --local --get core.hooksPath || true)
if [ "$current" = "$want" ]; then
    echo "install-hooks: already installed (core.hooksPath=$want)"
else
    if [ -n "$current" ] && [ "$force" -eq 0 ]; then
        echo "install-hooks: core.hooksPath is already '$current' (not '$want');" >&2
        echo "              pass --force to replace it" >&2
        exit 1
    fi
    # core.hooksPath overrides .git/hooks, so an existing hook there would stop
    # running silently. Do not do that without --force.
    legacy="$(git rev-parse --git-path hooks)/pre-commit"
    if [ -e "$legacy" ] && [ "$force" -eq 0 ]; then
        echo "install-hooks: $legacy exists and would stop running;" >&2
        echo "              pass --force to use the repo hook instead" >&2
        exit 1
    fi
    git config --local core.hooksPath "$want"
    echo "install-hooks: set core.hooksPath=$want"
fi

chmod +x "$hook"
echo "install-hooks: pre-commit active ($hook)"
