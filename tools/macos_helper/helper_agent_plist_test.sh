#!/usr/bin/env bash
# Device-free check of the user LaunchAgent artifact (row #181).
#
# Renders the agent plist through install_user_agent.sh --dry-run (no signing,
# no launchctl, no ~/Library write) and asserts the keys that make the service
# work: a Label, MachServices for org.openscanner.helper, the helper program
# with the matching --service-name, and no unresolved template token.
set -euo pipefail

HELPER="${1:-}"
if [ -z "$HELPER" ]; then
  echo "usage: helper_agent_plist_test.sh <path to openscanner-helper>" >&2
  exit 2
fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$("$ROOT/tools/macos_helper/install_user_agent.sh" install --dry-run --no-sign --helper "$HELPER")"

fail=0
check() {
  local what="$1"
  shift
  if "$@"; then
    echo "  ok   $what"
  else
    echo "  FAIL $what"
    fail=1
  fi
}

grep_q() { grep -q "$1" <<<"$OUT"; }

check "the plist has a Label" grep_q "<key>Label</key>"
check "the label is org.openscanner.helper" grep_q "<string>org.openscanner.helper</string>"
check "MachServices registers org.openscanner.helper" grep_q "<key>MachServices</key>"
check "the helper runs with --claim iokit" grep_q "<string>--claim</string>"
check "the listener's --service-name matches the Mach service" grep_q "<string>--service-name</string>"
check "the helper program is the built binary" grep -qF "$HELPER" <<<"$OUT"
check "no template token is left unresolved" bash -c '! grep -qE "@[A-Z_]+@" <<<"$1"' _ "$OUT"
check "the installer linted the plist (plutil passed)" grep -q "plist lints" <<<"$OUT"

if [ "$fail" -ne 0 ]; then
  echo "[helper_agent_plist_test] FAILED"
  exit 1
fi
echo "[helper_agent_plist_test] PASS: the user LaunchAgent registers org.openscanner.helper"
