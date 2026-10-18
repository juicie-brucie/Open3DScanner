#!/usr/bin/env bash
# Coordinate with another OpenCode server — typically a dispatcher running on a
# second machine (e.g. the Windows fork's dispatcher), registered in the
# OpenCode desktop app's server list.
#
# Usage:
#   remote-opencode.sh info    [--local | --machine NAME]
#   remote-opencode.sh active  [--machine NAME]
#   remote-opencode.sh sessions[--machine NAME]
#   remote-opencode.sh read    [--machine NAME] <sessionID> [limit]
#   remote-opencode.sh prompt  [--machine NAME] <sessionID> <text...>   # queued
#   remote-opencode.sh steer   [--machine NAME] <sessionID> <text...>   # steer
#   remote-opencode.sh send    [--machine NAME] <sessionID> <body-file>
#                              [--steer] [--force] [--dry-run]
#
# `send` is the path for work orders: the body comes from a FILE (never from
# argv), it is previewed (bytes, sha256, first/last lines) before it is posted,
# and it is refused when it looks like a body a shell already ate. Author a body
# with a QUOTED heredoc, so nothing in it is substituted:
#
#   cat > /tmp/order.txt <<'EOF'
#   ...prose with `backticks`, $vars and $(cmd) left literal...
#   EOF
#   scripts/remote-opencode.sh send --local <sessionID> /tmp/order.txt
#
# An unquoted <<EOF substitutes `backticks` and $(...) in the body: backticked
# prose is replaced by command output (usually nothing) and the order silently
# loses text. If the shell prints "command not found" while you author a body,
# the file is already wrong - delete it and write it again quoted.
#
# The server URL and password are read at runtime from the desktop app's state
# file (the same entry the app uses); they are never printed, echoed or logged.
# `--machine` defaults to the single server in the app's list; pass it when
# several servers are configured. `--local` targets THIS machine's own service
# (its port and password from ~/.config/opencode/service.json) - use it for
# same-machine sessions: with only a remote machine in the app's list, a plain
# invocation silently targets that machine, and a local session id then comes
# back as SessionNotFound. Override the state file with OPENCODE_DESKTOP_STATE.
set -euo pipefail

DB="${OPENCODE_DESKTOP_STATE:-$HOME/Library/Application Support/ai.opencode.desktop/drafts.sqlite}"

MACHINE=""; LOCAL=""; ARGS=()
for a in "$@"; do
  if [ "${SET_MACHINE:-0}" = "1" ]; then MACHINE="$a"; SET_MACHINE=0; else
    if [ "$a" = "--machine" ]; then SET_MACHINE=1
    elif [ "$a" = "--local" ]; then LOCAL=1
    else ARGS+=("$a"); fi
  fi
done
[ "${SET_MACHINE:-0}" = "1" ] && { echo "--machine needs a value" >&2; exit 2; }

if [ -n "$LOCAL" ]; then
  PORT=$(lsof -nP -iTCP -sTCP:LISTEN 2>/dev/null | awk '/opencode/ {print $9}' | head -1 | sed 's/.*://')
  [ -n "$PORT" ] || { echo "local: no opencode listener found" >&2; exit 2; }
  URL="http://127.0.0.1:$PORT"
  PASSWORD=$(jq -r .password "$HOME/.config/opencode/service.json")
else
  if [ ! -f "$DB" ]; then echo "state file not found: $DB" >&2; exit 2; fi
  STATE=$(sqlite3 "$DB" "select value from state where key='server'")
  if [ -z "$MACHINE" ]; then
    n=$(jq -r '[.list[] | select(.http.url)] | length' <<<"$STATE")
    if [ "$n" != "1" ]; then
      echo "configured servers:" >&2
      jq -r '.list[] | select(.http.url) | "  \(.displayName)"' <<<"$STATE" >&2
      echo "pass --machine NAME (or --local for this machine)" >&2; exit 2
    fi
    MACHINE=$(jq -r '.list[] | select(.http.url) | .displayName' <<<"$STATE")
  fi
  URL=$(jq -r --arg m "$MACHINE" '.list[] | select(.displayName==$m) | .http.url' <<<"$STATE")
  PASSWORD=$(jq -r --arg m "$MACHINE" '.list[] | select(.displayName==$m) | .http.password' <<<"$STATE")
  [ -n "$URL" ] && [ "$URL" != "null" ] || { echo "machine not found: $MACHINE" >&2; exit 2; }
fi

# The credential goes to curl through a stdin config (`-K -`), never through
# argv — so it is not visible to `ps` while a request is in flight.
creds_config() {
  printf 'user = "opencode:%s"\n' "$(printf '%s' "$PASSWORD" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')"
}

api() { # api <method> <path> [json]
  if [ "$#" -ge 3 ]; then
    creds_config | curl -sS -m 30 -K - -X "$1" -H 'Content-Type: application/json' -d "$3" "$URL$2"
  else
    creds_config | curl -sS -m 30 -K - -X "$1" "$URL$2"
  fi
}

verb="${ARGS[0]:-}"; rest=("${ARGS[@]:1}")
case "$verb" in
  info)
    api GET /api/info | jq -r '"version \(.version)  pid \(.pid)  platform paths: \(.paths.tmp // "?")"' ;;
  active)
    api GET /api/session/active | jq -r '.data | to_entries[] | "\(.key)  \(.value.type)"' ;;
  sessions)
    api GET /api/session | jq -r '(.data // .)[] | "\(.id)  \([.agent,.title]|map(select(.!=null))|join("  "))"' ;;
  read)
    sid="${rest[0]:?sessionID required}"; lim="${rest[1]:-6}"
    api GET "/api/session/$sid/message?limit=$lim" \
      | jq -r '[.data[]] | sort_by(.time.created) | .[] | .type + ": " + (((.content // []) | map(select(.type=="text")) | .[0].text // "") | gsub("\n"; " ") | .[0:160])' ;;
  prompt|steer)
    sid="${rest[0]:?sessionID required}"; text="${rest[*]:1}"
    [ "$verb" = "steer" ] && delivery="steer" || delivery="queue"
    body=$(jq -n --arg t "$text" --arg d "$delivery" '{text:$t, delivery:$d}')
    out=$(api POST "/api/session/$sid/prompt" "$body")
    if printf '%s' "$out" | jq -e '._tag == "SessionNotFoundError"' >/dev/null 2>&1; then
      echo "hint: no such session on this target - if it is a local session, pass --local" >&2
    fi
    printf '%s\n' "$out" ;;
  send)
    # send <sessionID> <body-file> [--steer] [--force] [--dry-run]
    # The body comes from a file, so it never travels through a shell; the
    # guards refuse a body a shell already ate (an unquoted heredoc).
    i=0; sid=""; file=""; delivery="queue"; force=0; dry=0
    while [ "$i" -lt "${#rest[@]}" ]; do
      a="${rest[$i]}"
      case "$a" in
        --steer)   delivery="steer" ;;
        --force)   force=1 ;;
        --dry-run) dry=1 ;;
        -*) echo "send: unknown flag $a" >&2; exit 2 ;;
        *) if [ -z "$sid" ]; then sid="$a"
           elif [ -z "$file" ]; then file="$a"
           else echo "send: unexpected argument $a" >&2; exit 2; fi ;;
      esac
      i=$((i + 1))
    done
    [ -n "$sid" ]  || { echo "send: sessionID required" >&2; exit 2; }
    [ -n "$file" ] || { echo "send: body file required" >&2; exit 2; }
    [ -f "$file" ] || { echo "send: body file not found: $file" >&2; exit 2; }
    body=$(cat "$file")
    [ -n "$body" ] || { echo "send: body file is empty" >&2; exit 2; }
    if [ "$force" = 0 ]; then
      if printf '%s' "$body" | grep -qE '(zsh|bash|sh): ?(line )?[0-9]+: .*(command not found|bad substitution|no matches found|unmatched|syntax error)'; then
        echo "send: REFUSING - the body carries shell error text, the mark of a garbled unquoted heredoc." >&2
        echo "send: re-author it with a quoted heredoc (cat > file <<'EOF') or pass --force if intentional." >&2
        exit 3
      fi
      nb=$(printf '%s' "$body" | tr -cd '`' | wc -c | tr -d ' ')
      if [ $((nb % 2)) -ne 0 ]; then
        echo "send: REFUSING - odd backtick count ($nb), the mark of a body a shell ate." >&2
        echo "send: re-author it with a quoted heredoc (cat > file <<'EOF') or pass --force if intentional." >&2
        exit 3
      fi
    fi
    bytes=$(printf '%s' "$body" | wc -c | tr -d ' ')
    sum=$(printf '%s' "$body" | shasum -a 256 | cut -c1-12)
    echo "--- send: $bytes bytes, sha256 $sum, delivery $delivery ---" >&2
    printf '%s\n' "$body" | sed -n '1,3p' >&2
    lines=$(printf '%s\n' "$body" | wc -l | tr -d ' ')
    if [ "$lines" -gt 4 ]; then echo "    ... ($lines lines)" >&2; printf '%s\n' "$body" | tail -1 >&2; fi
    echo "--- end ---" >&2
    if [ "$dry" = 1 ]; then echo "send: dry run - not posted" >&2; exit 0; fi
    payload=$(jq -n --arg t "$body" --arg d "$delivery" '{text:$t, delivery:$d}')
    out=$(api POST "/api/session/$sid/prompt" "$payload")
    if printf '%s' "$out" | jq -e '._tag == "SessionNotFoundError"' >/dev/null 2>&1; then
      echo "hint: no such session on this target - if it is a local session, pass --local" >&2
    fi
    printf '%s\n' "$out" ;;
  *)
    awk 'NR>1 && /^set /{exit} NR>1{print}' "$0" >&2; exit 2 ;;
esac
