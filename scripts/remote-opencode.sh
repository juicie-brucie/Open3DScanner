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
  *)
    sed -n '2,20p' "$0" >&2; exit 2 ;;
esac
