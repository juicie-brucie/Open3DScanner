#!/usr/bin/env bash
# new-session.sh — create a session for an agent in a room (row #109).
#
# Usage: scripts/new-session.sh <agent> <room> [--dry-run] [--title <title>]
#
#   <agent>    builder | verifier | recon | dispatch | planner | recon-sweep
#              (the full "openscanner/<name>" form is accepted too)
#   <room>     the room's worktree path, or a room name from `git worktree list`
#              (e.g. "Open3DScanner-wt-build5" or "build5")
#
# The recreation recipe in one command:
#   1. resolve the room (a path or a worktree name) and validate that it is a
#      worktree of a repository;
#   2. read the agent's configured model from the running server
#      (GET /api/agent — the same source scripts/check-agents.py compares
#      against), so the session is created with that model EXPLICITLY instead
#      of inheriting whatever the server would pick;
#   3. create the session (POST /api/session) with the agent, the model and
#      the room as its location;
#   4. print the new session id.
#
# The agent files are never read or written: the server's own /api/agent view
# is the model of record, and a malformed agent file (a permission list that
# does not parse) silently drops the agent — so the file is the wrong place to
# look.
#
# No secret is printed: the OpenCode password is read at runtime from
# ~/.config/opencode/service.json and handed to curl through stdin (-K -), the
# same way scripts/pipeline-status.sh does it. The port is discovered from the
# listening socket.
#
# Degrades with a clear message and exit 2 when the local server is not
# running, when the credentials are missing, or when the agent/room cannot be
# resolved. --dry-run resolves and prints everything without creating.
#
# Dependencies: bash, curl, jq, git (lsof for the port).
set -uo pipefail

SERVICE="${OPENSCANNER_SERVICE:-$HOME/.config/opencode/service.json}"
DRY_RUN=0
TITLE=""
AGENT_ARG=""
ROOM_ARG=""

die() { printf 'new-session: %s\n' "$1" >&2; exit 2; }
note() { printf '%s\n' "$1"; }

usage() {
    sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1; shift ;;
        --title) [ "$#" -ge 2 ] || die "--title needs a value"; TITLE="$2"; shift 2 ;;
        -h|--help) usage ;;
        --*) die "unknown option '$1' (see --help)" ;;
        *)
            if [ -z "$AGENT_ARG" ]; then AGENT_ARG="$1"
            elif [ -z "$ROOM_ARG" ]; then ROOM_ARG="$1"
            else die "unexpected argument '$1' (see --help)"
            fi
            shift ;;
    esac
done

[ -n "$AGENT_ARG" ] || die "usage: new-session.sh <agent> <room> [--dry-run] [--title <title>]"
[ -n "$ROOM_ARG" ] || die "usage: new-session.sh <agent> <room> [--dry-run] [--title <title>]"

command -v curl >/dev/null 2>&1 || die "curl is not installed."
command -v jq >/dev/null 2>&1 || die "jq is not installed."
command -v git >/dev/null 2>&1 || die "git is not installed."

# --- the room ----------------------------------------------------------------
# A path that exists wins; otherwise the argument is matched against the
# worktrees of the repository this script lives in.
resolve_room() {
    local arg="$1" candidate
    if [ -d "$arg" ]; then
        (cd "$arg" && pwd -P)
        return 0
    fi
    local repo_root
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
    while IFS= read -r candidate; do
        local base
        base="$(basename "$candidate")"
        case "$base" in
            *"$arg") printf '%s\n' "$candidate"; return 0 ;;
        esac
        if [ "$arg" = "${base##*-wt-}" ]; then printf '%s\n' "$candidate"; return 0; fi
    done < <(git -C "$repo_root" worktree list --porcelain 2>/dev/null | awk '/^worktree /{print $2}')
    return 1
}

ROOM="$(resolve_room "$ROOM_ARG")" || die "room '$ROOM_ARG' not found: pass the worktree path, or a name from 'git worktree list' (create it with: git worktree add <path> -b <branch> origin/main)."
[ -d "$ROOM" ] || die "room '$ROOM' is not a directory."
ROOM_TOP="$(git -C "$ROOM" rev-parse --show-toplevel 2>/dev/null)" || die "room '$ROOM' is not a git worktree."
[ "$ROOM_TOP" = "$ROOM" ] || die "room '$ROOM' is inside a worktree whose root is '$ROOM_TOP' — pass the worktree root."
ROOM_BRANCH="$(git -C "$ROOM" symbolic-ref --short -q HEAD || echo '(detached)')"

# --- the server --------------------------------------------------------------
PORT=$(lsof -nP -iTCP -sTCP:LISTEN 2>/dev/null | awk '/opencode/ {print $9}' | head -1 | sed 's/.*://')
[ -n "${PORT:-}" ] || die "no local OpenCode server is listening — start OpenCode, then re-run this command."
[ -f "$SERVICE" ] || die "service credentials not found at $SERVICE."

PASS=$(jq -r '.password // empty' "$SERVICE" 2>/dev/null) || PASS=""
[ -n "$PASS" ] || die "no password in $SERVICE."

BASE="http://127.0.0.1:$PORT"
# The credential goes to curl through its stdin config; it is never echoed.
api() {
    local method="$1" path="$2" body="${3:-}"
    if [ -n "$body" ]; then
        printf 'user = "opencode:%s"\n' "$PASS" |
            curl -fsS -m 15 -K - -X "$method" -H 'Content-Type: application/json' -d "$body" "$BASE$path"
    else
        printf 'user = "opencode:%s"\n' "$PASS" |
            curl -fsS -m 15 -K - -X "$method" "$BASE$path"
    fi
}

# --- the agent and its model -------------------------------------------------
AGENTS_JSON=$(api GET /api/agent 2>/dev/null) || die "could not query $BASE/api/agent."
[ -n "$AGENTS_JSON" ] || die "could not query $BASE/api/agent (empty answer)."

AGENT_ID="$AGENT_ARG"
case "$AGENT_ARG" in
    */*) : ;;                       # already the full id
    *)   AGENT_ID="openscanner/$AGENT_ARG" ;;
esac

AGENT_MODEL=$(jq -r --arg id "$AGENT_ID" '
    (.data // .) | map(select(.id == $id)) | .[0].model // empty' <<<"$AGENTS_JSON")
[ -n "$AGENT_MODEL" ] || die "agent '$AGENT_ID' is not registered with the server (GET /api/agent lists the ids)."
[ "$AGENT_MODEL" != "null" ] || die "agent '$AGENT_ID' has no configured model."

# Model.Ref wants exactly {id, providerID, variant?}.
MODEL_JSON=$(jq -c '{id: .id, providerID: .providerID}
              + (if .variant then {variant: .variant} else {} end)' <<<"$AGENT_MODEL")
MODEL_TEXT=$(jq -r '(.providerID // "?") + "/" + (.id // "?") + (if .variant then "#" + .variant else "" end)' <<<"$MODEL_JSON")

# The title: unique per room by default, overridable.
[ -n "$TITLE" ] || TITLE="$AGENT_ARG session $(basename "$ROOM")"

BODY=$(jq -n --arg title "$TITLE" --arg agent "$AGENT_ID" \
          --argjson model "$MODEL_JSON" --arg dir "$ROOM" \
          '{title: $title, agent: $agent, model: $model, location: {directory: $dir}}')

note "new-session: agent   $AGENT_ID"
note "new-session: model   $MODEL_TEXT"
note "new-session: room    $ROOM (branch: $ROOM_BRANCH)"
note "new-session: title   $TITLE"

if [ "$DRY_RUN" = "1" ]; then
    note "new-session: DRY RUN — nothing was created. The request body was:"
    jq . <<<"$BODY"
    exit 0
fi

OUT=$(api POST /api/session "$BODY" 2>/dev/null) || die "the server refused the session creation (POST /api/session failed)."
SESSION_ID=$(jq -r '.data.id // empty' <<<"$OUT" 2>/dev/null)
[ -n "$SESSION_ID" ] || die "the server answered without a session id: $(jq -c . <<<"$OUT" 2>/dev/null | head -c 300)"

note "new-session: created $SESSION_ID"
note "new-session: use it with: scripts/remote-opencode.sh prompt $SESSION_ID \"<your task>\""
exit 0
