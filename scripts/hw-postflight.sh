#!/usr/bin/env bash
# hw-postflight.sh — deterministic crash-report sweep for the hw-test agent.
#
#   scripts/hw-postflight.sh <record-slug> <run-start 'YYYY-MM-DD HH:MM:SS'> <target>
#
# A crash report belongs to the run when BOTH hold, read from the report itself:
#   - its header JSON's `app_name` (or `name`) equals <target>; and
#   - its header `timestamp` is at or after the run start.
# If several reports match, the EARLIEST is archived (the crash that ended the
# run) and the others are listed with their paths, so nothing is silently
# dropped. If none match, the script says so explicitly — that sentence is the
# only thing a record may quote for "no crash report".
#
# Why content and not just the filename: filenames can be truncated for long
# process names, a same-named unrelated process can crash in the window, and the
# old filename-scan picked whichever entry `ls -t` happened to leave last.
#
# Exit 0 always (a missing crash report is a valid outcome); exit 2 on bad usage.
set -euo pipefail

SLUG="${1:-}"
START="${2:-}"
TARGET="${3:-}"
usage() { echo "usage: hw-postflight.sh <record-slug> 'YYYY-MM-DD HH:MM:SS' <target>" >&2; exit 2; }
[ -n "$SLUG" ] && [ -n "$START" ] && [ -n "$TARGET" ] || usage

DR="$HOME/Library/Logs/DiagnosticReports"
RESULTS="tests/results"
[ -d "$RESULTS" ] || { echo "hw-postflight: no tests/results/ here — run from the worktree root." >&2; exit 2; }
if [ ! -d "$DR" ]; then
  echo "hw-postflight: no DiagnosticReports dir on this machine — nothing to sweep."
  exit 0
fi

START_EPOCH="$(date -j -f '%Y-%m-%d %H:%M:%S' "$START" '+%s' 2>/dev/null || echo 0)"
[ "$START_EPOCH" -gt 0 ] || { echo "hw-postflight: cannot parse run start '$START' (want 'YYYY-MM-DD HH:MM:SS')." >&2; exit 2; }

# Reports can take a few seconds to land; give them a moment.
sleep 3

# Collect "epoch|path" for every report whose header names the target and whose
# header timestamp is at/after the start.
MATCHES=""
while IFS= read -r f; do
  [ -n "$f" ] || continue
  m="$(stat -f '%m' "$f" 2>/dev/null || echo 0)"
  [ "$m" -ge "$START_EPOCH" ] || continue           # cheap prefilter; header decides
  hdr="$(head -1 "$f" 2>/dev/null || true)"
  [ -n "$hdr" ] || continue
  app="$(printf '%s' "$hdr" | jq -r '.app_name // .name // empty' 2>/dev/null || true)"
  [ "$app" = "$TARGET" ] || continue
  ts="$(printf '%s' "$hdr" | jq -r '.timestamp // empty' 2>/dev/null || true)"
  ts_clean="${ts%%.*}"                              # '2026-10-19 14:59:54.00 -0400' -> '2026-10-19 14:59:54'
  ts_epoch="$(date -j -f '%Y-%m-%d %H:%M:%S' "$ts_clean" '+%s' 2>/dev/null || echo "$m")"
  [ "$ts_epoch" -ge "$START_EPOCH" ] || continue
  MATCHES="${MATCHES}${ts_epoch}|${f}"$'\n'
done < <(ls -1 "$DR"/*.ips 2>/dev/null || true)

COUNT="$(printf '%s' "$MATCHES" | grep -c '|' || true)"
if [ "${COUNT:-0}" -eq 0 ]; then
  echo "hw-postflight: no crash report for '$TARGET' at/after $START — safe to record 'no crash report (checked DiagnosticReports for $TARGET at/after the run start)'."
  exit 0
fi

# Deterministic choice: lowest header timestamp, then lexicographic path.
FOUND="$(printf '%s' "$MATCHES" | sort -t'|' -k1,1n -k2,2 | head -1 | cut -d'|' -f2)"
DEST="$RESULTS/$SLUG.crash.ips"
cp "$FOUND" "$DEST"
SHA="$(shasum -a 256 "$DEST" | awk '{print $1}')"
INC="$(head -1 "$FOUND" | jq -r '.incident_id // empty' 2>/dev/null || true)"

echo "hw-postflight: crash report matched by app_name + timestamp."
echo "  source: $(basename "$FOUND")"
echo "  copied: $DEST"
echo "  sha256: $SHA"
[ -n "$INC" ] && echo "  incident: $INC"
if [ "${COUNT:-0}" -gt 1 ]; then
  echo "  note: $COUNT report(s) matched; archived the EARLIEST. The others:"
  printf '%s' "$MATCHES" | sort -t'|' -k1,1n -k2,2 | tail -n +2 | cut -d'|' -f2 | sed 's/^/    /'
fi
echo
echo "----8<---- reference this in the record ----8<----"
echo "- Crash report: [\`$SLUG.crash.ips\`](./$SLUG.crash.ips) from \`$(basename "$FOUND")\`, sha256 \`$SHA\`${INC:+ (incident \`$INC\`)}"
echo "----8<------------------------------------------8<----"
