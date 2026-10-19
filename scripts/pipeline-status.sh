#!/usr/bin/env bash
# pipeline-status.sh — the dispatcher's check-in in one command.
#
# Prints, one screen:
#   1. open PRs: number, tier, merge state, latest VERDICT (state + sha),
#      flagged when the verdict sha is not the head sha;
#   2. live sessions from the local OpenCode API: id, agent, model, outcome,
#      tokens.input (flagged over a threshold);
#   3. non-DONE PLAN.md rows;
#   4. name-anchored process checks (pgrep -x, never a substring match);
#   5. each git worktree's branch, ahead/behind origin/main, dirty and unpushed.
#
# Usage:
#   scripts/pipeline-status.sh [--help]
#
# Environment:
#   OPENSCANNER_TOKEN_THRESHOLD   input-token flag (default 1000000)
#   OPENSCANNER_ROOMS             space-separated worktree paths (default: git worktree list)
#   OPENSCANNER_REPO              owner/repo (default juicie-brucie/Open3DScanner)
#
# No secrets are printed: the OpenCode password is read at runtime from
# ~/.config/opencode/service.json and passed to curl through stdin (-K -), and
# the port is discovered from the listening socket.
#
# Degrades gracefully: a missing gh/API prints that section as unavailable and
# the script still exits 0. Dependencies: bash, jq, gh, curl, git.

set -uo pipefail

REPO="${OPENSCANNER_REPO:-juicie-brucie/Open3DScanner}"
SERVICE="${OPENSCANNER_SERVICE:-$HOME/.config/opencode/service.json}"
THRESHOLD="${OPENSCANNER_TOKEN_THRESHOLD:-1000000}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    sed -n '2,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
fi

case "$THRESHOLD" in
    ''|*[!0-9]*) echo "pipeline-status: OPENSCANNER_TOKEN_THRESHOLD must be an integer; using 1000000" >&2; THRESHOLD=1000000 ;;
esac

hr() { printf '== %s ==\n' "$1"; }

# --- 1. open PRs -----------------------------------------------------------
hr "PRs"
if ! command -v gh >/dev/null 2>&1; then
    echo "unavailable: gh not installed"
else
    prs=$(gh pr list --repo "$REPO" --state open --limit 30 \
            --json number,headRefOid,headRefName,mergeStateStatus,mergeable 2>/dev/null)
    if [ -z "$prs" ]; then
        echo "unavailable: gh pr list failed (auth/network?)"
    elif [ "$(jq 'length' <<<"$prs")" = "0" ]; then
        echo "none open"
    else
        jq -r '.[] | [.number, .headRefOid, .headRefName, (.mergeStateStatus // "?"), (.mergeable // "?")] | @tsv' <<<"$prs" |
        while IFS=$'\t' read -r num head branch merge mergeable; do
            tier=$(python3 "$REPO_ROOT/scripts/verify-pr.py" --tier "$num" 2>/dev/null | head -1 || true)
            [ -n "$tier" ] || tier="?"
            verdict=$(gh api "repos/$REPO/issues/$num/comments" \
                        --jq '[.[]|select(.body|startswith("VERDICT:"))][-1].body' 2>/dev/null | head -1 || true)
            vtext="no verdict"
            if [ -n "$verdict" ]; then
                if [[ "$verdict" =~ ^VERDICT:\ (.*)\ @\ ([0-9a-f]+) ]]; then
                    vstate="${BASH_REMATCH[1]}"
                    vsha="${BASH_REMATCH[2]}"
                    vtext="$vstate @ ${vsha:0:8}"
                    if [[ "$head" != "$vsha"* ]]; then
                        vtext="$vtext STALE(head ${head:0:8})"
                    fi
                else
                    vtext="$verdict"
                fi
            fi
            printf '#%-4s tier=%-6s merge=%s/%s head=%s branch=%s  %s\n' \
                "$num" "$tier" "$merge" "$mergeable" "${head:0:8}" "$branch" "$vtext"
        done
    fi
fi

# --- 2. live sessions ------------------------------------------------------
hr "Sessions"
PORT=$(lsof -nP -iTCP -sTCP:LISTEN 2>/dev/null | awk '/opencode/ {print $9}' | head -1 | sed 's/.*://')
if [ -z "${PORT:-}" ]; then
    echo "unavailable: no local OpenCode listener"
elif ! command -v curl >/dev/null 2>&1 || ! command -v jq >/dev/null 2>&1; then
    echo "unavailable: curl/jq missing"
elif [ ! -f "$SERVICE" ]; then
    echo "unavailable: service credentials not found"
else
    api() {
        local pass
        pass=$(jq -r '.password // empty' "$SERVICE" 2>/dev/null) || return 1
        [ -n "$pass" ] || return 1
        # Credential goes through curl's stdin config; never echoed.
        # -f: a non-2xx (e.g. a rotated password's 401) must yield an empty
        # body, so the caller takes its "unavailable" path instead of reading
        # the error JSON as a session list.
        printf 'user = "opencode:%s"\n' "$pass" | curl -fsS -m 5 -K - "http://127.0.0.1:$PORT$1"
    }
    all=$(api /api/session 2>/dev/null)
    active=$(api /api/session/active 2>/dev/null)
    if [ -z "$all" ]; then
        echo "unavailable: /api/session failed"
    else
        live_ids=$(jq -c '(.data // {}) | keys' <<<"${active:-{\}}" 2>/dev/null || echo '[]')
        count=$(jq '(.data // .) | length' <<<"$all" 2>/dev/null || echo 0)
        label="active"
        if [ "$(jq 'length' <<<"$live_ids" 2>/dev/null || echo 0)" = "0" ]; then
            label="recent (no active session; showing 3)"
            live_ids='[]'
        fi
        printf '%s (%s total)\n' "$label" "$count"
        jq -r --argjson th "$THRESHOLD" --argjson live "$live_ids" '
            (.data // .)
            | sort_by(.time.updated) | reverse
            | (if ($live|length) > 0 then map(select(.id as $i | $live | index($i))) else .[:3] end)
            | .[] |
            [ (.id[0:16]),
              (.agent // "?"),
              ((.model.providerID // "?") + "/" + (.model.id // "?")),
              (.outcome // "running"),
              ((.tokens.input // 0)|tostring),
              (if ((.tokens.input // 0) > $th) then "OVER" else "" end)
            ] | @tsv' <<<"$all" 2>/dev/null |
        while IFS=$'\t' read -r id agent model outcome tokens over; do
            printf '%-18s %-24s %-40s %-10s tokens.input=%-9s %s\n' \
                "$id" "$agent" "$model" "$outcome" "$tokens" "$over"
        done
    fi
fi

# --- 3. board (non-DONE) ---------------------------------------------------
hr "Board (non-DONE)"
if [ ! -f "$REPO_ROOT/docs/recomp/PLAN.md" ]; then
    echo "unavailable: PLAN.md not found"
else
    awk -F'|' '
        /^\| [0-9]+ / {
            num=$2; target=$3; status=$4;
            gsub(/^[ \t]+|[ \t]+$/, "", num);
            gsub(/^[ \t]+|[ \t]+$/, "", target);
            gsub(/^[ \t]+|[ \t]+$/, "", status);
            if (status != "DONE") {
                if (length(target) > 72) target = substr(target, 1, 69) "…";
                printf "#%-4s %-10s %s\n", num, status, target;
            }
        }' "$REPO_ROOT/docs/recomp/PLAN.md"
fi

# --- 4. processes (name-anchored) -----------------------------------------
hr "Processes (pgrep -x)"
if ! command -v pgrep >/dev/null 2>&1; then
    echo "unavailable: pgrep missing"
else
    for name in scan_server raptor_client smoke_test live_capture_test \
                device_message_listener_test data_process_message_listener_test \
                capture_pipeline_smoke_test mesh_convert; do
        pids=$(pgrep -x "$name" 2>/dev/null | tr '\n' ' ')
        if [ -n "$pids" ]; then
            info=$(ps -o pid=,etime=,pcpu=,comm= -p $pids 2>/dev/null | tr '\n' '; ' | sed 's/; $//')
            printf '%-36s %s\n' "$name" "$info"
        else
            printf '%-36s -\n' "$name"
        fi
    done
fi

# --- 5. rooms --------------------------------------------------------------
hr "Rooms"
rooms=()
if [ -n "${OPENSCANNER_ROOMS:-}" ]; then
    read -r -a rooms <<<"$OPENSCANNER_ROOMS"
else
    while IFS= read -r line; do
        case "$line" in
            "worktree "*) rooms+=("${line#worktree }") ;;
        esac
    done < <(git -C "$REPO_ROOT" worktree list --porcelain 2>/dev/null)
fi
if [ "${#rooms[@]}" = "0" ]; then
    echo "unavailable: no worktrees found"
else
    for d in "${rooms[@]}"; do
        [ -d "$d" ] || { printf '%s\n  (missing)\n' "$d"; continue; }
        branch=$(git -C "$d" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "?")
        counts=$(git -C "$d" rev-list --left-right --count origin/main...HEAD 2>/dev/null || echo "? ?")
        behind=$(awk '{print $1}' <<<"$counts")
        ahead=$(awk '{print $2}' <<<"$counts")
        dirty=$(git -C "$d" status --porcelain 2>/dev/null | wc -l | tr -d ' ')
        if git -C "$d" rev-parse --abbrev-ref '@{u}' >/dev/null 2>&1; then
            unpushed=$(git -C "$d" rev-list --count '@{u}..HEAD' 2>/dev/null || echo "?")
            upstate="$unpushed unpushed"
        else
            upstate="no upstream"
        fi
        printf '%-58s %-32s behind=%-3s ahead=%-3s dirty=%-3s %s\n' \
            "$d" "$branch" "$behind" "$ahead" "$dirty" "$upstate"
    done
fi

exit 0
