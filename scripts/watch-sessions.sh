#!/usr/bin/env bash
# watch-sessions.sh — dispatcher tooling: one backgrounded process that waits for
# other agent sessions and for PRs to reach a state the dispatcher must act on,
# then prints one summary line per item.
#
# Usage:
#   scripts/watch-sessions.sh [options] <ses_... | PR#> [<ses_... | PR#> ...]
#
# Options:
#   --timeout SEC    per-item cap (default 1800)
#   --interval SEC   PR poll interval (default 15)
#   --grace SEC      session idle-at-start grace (default 20)
#   --base URL       API base (default: auto-detect the listening opencode port)
#   -h, --help       this text
#
# NOTE (dispatcher usage): this process exits only when EVERY item has settled, and
# the dispatcher's wake-up is tied to this command finishing — so one watcher over
# several items delays every wake to the slowest one. Launch ONE watcher per item
# (separate backgrounded calls) so each completion wakes you independently; use a
# multi-item invocation only when you genuinely want the whole set to settle before
# acting (e.g. a final round summary).
#
# Sessions: blocks on POST /api/experimental/session/<id>/wait (204 when the
# session goes idle; blocks while it runs), then confirms a message landed after
# the watch began. A session that is idle at watch start and produces no new
# activity is reported as such after the grace period — it never waits forever
# on a session that was already done.
#
# PRs: polls gh until the PR is MERGED/CLOSED, conflicts with main, has a fresh
# VERDICT comment on the current head (NEEDS CHANGE, or MERGEABLE without
# auto-merge armed), or has a failing check. MERGEABLE with auto-merge armed
# keeps waiting for the merge itself.
#
# Read-only: API GETs, the wait call, and gh reads. No repo writes, no vendor
# access, no merges. Not an agent — no wall applies to it.
set -u

TIMEOUT=1800; INTERVAL=15; GRACE=20; BASE=""
ITEMS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --timeout)  TIMEOUT="$2";  shift 2 ;;
    --interval) INTERVAL="$2"; shift 2 ;;
    --grace)    GRACE="$2";    shift 2 ;;
    --base)     BASE="$2";     shift 2 ;;
    -h|--help)  sed -n '2,26p' "$0"; exit 0 ;;
    --*)        echo "watch-sessions: unknown option: $1" >&2; exit 2 ;;
    *)          ITEMS+=("$1"); shift ;;
  esac
done
if [ ${#ITEMS[@]} -eq 0 ]; then
  echo "usage: watch-sessions.sh [--timeout S] [--interval S] [--grace S] [--base URL] <ses_... | PR#> ..." >&2
  exit 2
fi

# --- auth: read at runtime, never echoed -------------------------------------
SERVICE="$HOME/.config/opencode/service.json"
[ -r "$SERVICE" ] || { echo "watch-sessions: cannot read $SERVICE" >&2; exit 2; }
PASS=$(jq -r '.password // empty' "$SERVICE" 2>/dev/null)
[ -n "$PASS" ] || { echo "watch-sessions: no password in $SERVICE" >&2; exit 2; }
NETRC=$(mktemp) || exit 2
WORK=$(mktemp -d) || exit 2
chmod 600 "$NETRC"
printf 'machine 127.0.0.1 login opencode password %s\n' "$PASS" > "$NETRC"
unset PASS
cleanup() { rm -f "$NETRC"; rm -rf "$WORK"; }
trap cleanup EXIT INT TERM

api() { curl -sS -m 20 --netrc-file "$NETRC" "$@"; }

# --- base URL: the opencode server picks its port at start -------------------
if [ -z "$BASE" ]; then
  PORT=$(lsof -nP -iTCP -sTCP:LISTEN 2>/dev/null | awk '/opencode-/ {print $9}' | head -1 | sed 's/.*://')
  if [ -z "$PORT" ]; then
    echo "watch-sessions: no listening opencode server found (pass --base)" >&2
    exit 2
  fi
  BASE="http://127.0.0.1:$PORT"
fi

REPO=$(gh repo view --json nameWithOwner -q .nameWithOwner 2>/dev/null || true)

# --- helpers -----------------------------------------------------------------
# session_line <id> — "outcome=... — \"<first line of last assistant text>\""
session_line() {
  local id="$1" out text
  out=$(api "$BASE/api/session/$id" | jq -r '.data.outcome // "unknown"' 2>/dev/null)
  text=$(api "$BASE/api/session/$id/message?limit=10" \
    | jq -r '[.data[]|select(.type=="assistant")]|max_by(.time.created)|((.content//[])|map(select(.type=="text")|.text)|join(" "))' 2>/dev/null \
    | tr '\n' ' ' | cut -c1-160)
  printf 'outcome=%s — "%s"' "$out" "$text"
}

# watch_session <id> <outfile>
watch_session() {
  local id="$1" out="$2" deadline=$(( $(date +%s) + TIMEOUT ))
  local baseline resp last_time last_type idle_ticks=0 remain
  baseline=$(api "$BASE/api/session/$id/message?limit=1" | jq -r '.data[-1].time.created // 0' 2>/dev/null)
  printf '[session] %s: watching (baseline last-message %s)\n' "$id" "$baseline" > "$out"
  while :; do
    if [ "$(date +%s)" -ge "$deadline" ]; then
      printf '[session] %s: TIMEOUT — %s\n' "$id" "$(session_line "$id")" > "$out"
      return 1
    fi
    if api "$BASE/api/session/active" | jq -e --arg id "$id" '.data[$id]' >/dev/null 2>&1; then
      idle_ticks=0
      remain=$(( deadline - $(date +%s) )); [ "$remain" -lt 5 ] && remain=5
      curl -sS -m "$remain" --netrc-file "$NETRC" -X POST \
        "$BASE/api/experimental/session/$id/wait" -o /dev/null 2>/dev/null || true
      continue
    fi
    resp=$(api "$BASE/api/session/$id/message?limit=1")
    last_time=$(printf '%s' "$resp" | jq -r '.data[-1].time.created // 0' 2>/dev/null)
    last_type=$(printf '%s' "$resp" | jq -r '.data[-1].type // "?"' 2>/dev/null)
    if [ "$last_time" -gt "$baseline" ] 2>/dev/null; then
      printf '[session] %s: done (new %s message at %s) — %s\n' \
        "$id" "$last_type" "$last_time" "$(session_line "$id")" > "$out"
      return 0
    fi
    idle_ticks=$(( idle_ticks + 1 ))
    if [ $(( idle_ticks * 2 )) -ge "$GRACE" ]; then
      printf '[session] %s: idle at watch start, no new activity (%ss) — %s\n' \
        "$id" "$GRACE" "$(session_line "$id")" > "$out"
      return 0
    fi
    sleep 2
  done
}

# watch_pr <number> <outfile>
watch_pr() {
  local n="$1" out="$2" deadline=$(( $(date +%s) + TIMEOUT ))
  local json state mss head armed vline verdict fails
  printf '[pr] #%s: watching\n' "$n" > "$out"
  while :; do
    json=$(gh pr view "$n" --json state,mergeStateStatus,headRefOid,autoMergeRequest,url 2>/dev/null) || {
      printf '[pr] #%s: not found or not readable\n' "$n" > "$out"; return 1; }
    state=$(printf '%s' "$json" | jq -r '.state')
    mss=$(printf '%s' "$json" | jq -r '.mergeStateStatus // "?"')
    head=$(printf '%s' "$json" | jq -r '.headRefOid // ""')
    armed=$(printf '%s' "$json" | jq -r '.autoMergeRequest != null')
    case "$state" in
      MERGED|CLOSED)
        printf '[pr] #%s: %s\n' "$n" "$state" > "$out"; return 0 ;;
    esac
    if [ "$mss" = "DIRTY" ]; then
      printf '[pr] #%s: CONFLICTING with main — needs a rebase\n' "$n" > "$out"; return 0
    fi
    if [ -n "$REPO" ]; then
      vline=$(gh api "repos/$REPO/issues/$n/comments" \
        --jq '[.[]|select(.body|startswith("VERDICT:"))]|last|.body' 2>/dev/null | head -1)
      if [ -n "$vline" ] && printf '%s' "$vline" | grep -q "$head"; then
        verdict=$(printf '%s' "$vline" | sed -n 's/^VERDICT: \([A-Z ]*\) @.*/\1/p')
        if [ "$verdict" = "NEEDS CHANGE" ]; then
          printf '[pr] #%s: NEEDS CHANGE @ %s — needs a fix\n' "$n" "${head:0:7}" > "$out"; return 0
        fi
        if [ "$verdict" = "MERGEABLE" ] && [ "$armed" != "true" ]; then
          printf '[pr] #%s: MERGEABLE @ %s but auto-merge is NOT armed — needs your action\n' \
            "$n" "${head:0:7}" > "$out"; return 0
        fi
      fi
    fi
    fails=$(gh pr checks "$n" --json state --jq '[.[]|select(.state=="FAILURE")]|length' 2>/dev/null || echo 0)
    if [ "${fails:-0}" != "0" ]; then
      printf '[pr] #%s: %s check(s) failing\n' "$n" "$fails" > "$out"; return 0
    fi
    if [ "$(date +%s)" -ge "$deadline" ]; then
      printf '[pr] #%s: TIMEOUT (state=%s)\n' "$n" "$state" > "$out"; return 1
    fi
    sleep "$INTERVAL"
  done
}

# --- launch one waiter per item ---------------------------------------------
echo "== watch-sessions: ${#ITEMS[@]} item(s) — $(date '+%H:%M:%S') — $BASE"
i=0
for it in "${ITEMS[@]}"; do
  i=$(( i + 1 ))
  f="$WORK/$(printf '%02d' "$i").out"
  case "$it" in
    ses_*)      watch_session "$it" "$f" & ;;
    \#*)        watch_pr "${it#\#}" "$f" & ;;
    ''|*[!0-9]*) echo "watch-sessions: skipping unrecognized item: $it" >&2 ;;
    *)          watch_pr "$it" "$f" & ;;
  esac
done
wait

rc=0
for f in "$WORK"/*.out; do
  [ -f "$f" ] || continue
  cat "$f"
  grep -q 'TIMEOUT\|not found' "$f" 2>/dev/null && rc=1
done
echo "== watch-sessions: settled — $(date '+%H:%M:%S')"
exit $rc
