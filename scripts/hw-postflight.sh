#!/usr/bin/env bash
# hw-postflight.sh — deterministic crash-report sweep for the hw-test agent.
#
#   scripts/hw-postflight.sh <record-slug> <run-start 'YYYY-MM-DD HH:MM:SS'> <target>
#   scripts/hw-postflight.sh --citation <raw-log> [<record-slug>]
#   scripts/hw-postflight.sh --selftest-citation
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

RESULTS="tests/results"

# The one hash implementation both modes use. macOS has shasum; coreutils'
# sha256sum is the fallback, so the citation mode is testable off-macOS too.
sha256_file() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  else
    echo "hw-postflight: no sha256 tool (need shasum or sha256sum)" >&2
    return 2
  fi
}

# Row #165 — the record factory's citation mode.
#
# Prints ONLY the record's Raw log line, so it can be captured or pasted:
#
#   - Raw log: [`<basename>`](./<basename>), sha256 `<64 hex>`
#
# The hash is the file's own, computed here: verify-pr.py's `archives` check
# resolves the nearest name in the line (the `(./…)` target) against
# tests/results/<basename> at the PR head and compares the two. A hand-typed
# hash that differs from the file is the one way this check fails (row #165).
# A 0-byte raw log — a hang's log — is a valid citation: its hash is the
# sha256 of the empty file.
#
# The line cites the file by BASENAME, because a record sits beside its raw
# log in tests/results/. The optional <record-slug> asserts the file is the
# one that record should cite, so a `<slug>.raw.log` typo is refused rather
# than printed.
citation_usage() {
  echo "usage: hw-postflight.sh --citation <raw-log path> [<record-slug>]" >&2
  exit 2
}
if [ "${1:-}" = "--citation" ]; then
  shift
  LOG="${1:-}"
  WANT_SLUG="${2:-}"
  [ -n "$LOG" ] || citation_usage
  [ "$#" -le 2 ] || citation_usage
  [ -f "$LOG" ] || { echo "hw-postflight: no such raw log: $LOG" >&2; exit 2; }
  BASE="$(basename "$LOG")"
  if [ -n "$WANT_SLUG" ] && [ "$BASE" != "$WANT_SLUG.raw.log" ]; then
    echo "hw-postflight: '$BASE' is not the log for slug '$WANT_SLUG' (want '$WANT_SLUG.raw.log')" >&2
    exit 2
  fi
  if [ "$(dirname "$LOG")" != "$RESULTS" ]; then
    echo "hw-postflight: note: the line cites '$BASE' by basename, so the file must sit in $RESULTS/ at the PR head." >&2
  fi
  SHA="$(sha256_file "$LOG")"
  echo "- Raw log: [\`$BASE\`](./$BASE), sha256 \`$SHA\`"
  exit 0
fi

# The citation mode's own test: exact lines for an empty and a non-empty log,
# the empty-file hash, and refusal of a missing file or a wrong slug. Prints
# PASS/FAIL per case and exits non-zero on any failure. No device, no repo
# state, nothing left behind.
if [ "${1:-}" = "--selftest-citation" ]; then
  TMP="$(mktemp -d)"
  trap 'rm -rf "$TMP"' EXIT
  FAILS=0
  pass() { echo "hw-postflight-selftest: $1 PASS"; }
  fail() { echo "hw-postflight-selftest: $1 FAIL"; FAILS=$((FAILS + 1)); }

  : > "$TMP/empty.raw.log"
  printf 'abc' > "$TMP/abc.raw.log"

  EMPTY_SHA=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
  ABC_SHA=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad

  if LINE="$(bash "$0" --citation "$TMP/empty.raw.log" 2>/dev/null)"; then
    WANT="- Raw log: [\`empty.raw.log\`](./empty.raw.log), sha256 \`$EMPTY_SHA\`"
    if [ "$LINE" = "$WANT" ]; then pass "a 0-byte log prints the empty-file sha256"; else
      fail "a 0-byte log prints the empty-file sha256"; echo "  got:  $LINE" >&2; echo "  want: $WANT" >&2
    fi
  else
    fail "a 0-byte log prints the empty-file sha256 (the generator exited non-zero)"
  fi

  if LINE="$(bash "$0" --citation "$TMP/abc.raw.log" 2>/dev/null)"; then
    WANT="- Raw log: [\`abc.raw.log\`](./abc.raw.log), sha256 \`$ABC_SHA\`"
    if [ "$LINE" = "$WANT" ]; then pass "a non-empty log prints its computed sha256"; else
      fail "a non-empty log prints its computed sha256"; echo "  got:  $LINE" >&2; echo "  want: $WANT" >&2
    fi
  else
    fail "a non-empty log prints its computed sha256 (the generator exited non-zero)"
  fi

  if LINE="$(bash "$0" --citation "$TMP/empty.raw.log" empty 2>/dev/null)"; then
    case "$LINE" in
      "- Raw log: [\`empty.raw.log\`](./empty.raw.log), sha256 "*)
        pass "the record slug is accepted when it matches the log" ;;
      *) fail "the record slug is accepted when it matches the log" ;;
    esac
  else
    fail "the record slug is accepted when it matches the log (the generator exited non-zero)"
  fi

  if bash "$0" --citation "$TMP/empty.raw.log" another >/dev/null 2>&1; then
    fail "a log that is not the slug's own is refused"
  else
    pass "a log that is not the slug's own is refused"
  fi

  if bash "$0" --citation "$TMP/absent.raw.log" >/dev/null 2>&1; then
    fail "a missing raw log is refused"
  else
    pass "a missing raw log is refused"
  fi

  if [ "$FAILS" -eq 0 ]; then
    echo "hw-postflight-selftest: ok (0 failure(s))"
    exit 0
  fi
  echo "hw-postflight-selftest: FAILED ($FAILS failure(s))"
  exit 1
fi

SLUG="${1:-}"
START="${2:-}"
TARGET="${3:-}"
usage() { echo "usage: hw-postflight.sh <record-slug> 'YYYY-MM-DD HH:MM:SS' <target>" >&2; exit 2; }
[ -n "$SLUG" ] && [ -n "$START" ] && [ -n "$TARGET" ] || usage

DR="$HOME/Library/Logs/DiagnosticReports"
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
SHA="$(sha256_file "$DEST")"
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
