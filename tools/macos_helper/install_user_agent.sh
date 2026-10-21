#!/usr/bin/env bash
# Register openscanner-helper as a user LaunchAgent (row #181, per #177).
#
# User scope only: this writes ~/Library/LaunchAgents/org.openscanner.helper.plist
# and bootstraps it in gui/$(id -u). No sudo, no /Library, no system daemon.
# The plist's MachServices key registers org.openscanner.helper for the session;
# the helper's XPC listener attaches to it, and raptor_shim's claim query can
# then reach it.
#
#   tools/macos_helper/install_user_agent.sh install [--helper PATH] [--identity NAME] [--no-sign] [--dry-run]
#   tools/macos_helper/install_user_agent.sh status
#   tools/macos_helper/install_user_agent.sh uninstall
#
# install dev-signs the helper with dev_sign.sh unless --no-sign is given.
# --dry-run renders and lints the plist in a temp file and stops before
# launchctl: no signing, no registration, no service start.
set -euo pipefail

LABEL="org.openscanner.helper"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEMPLATE="$ROOT/tools/macos_helper/launchd/org.openscanner.helper-agent.plist.in"
DEST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/openscanner-helper.log"
HELPER="${HELPER:-$ROOT/build/tools/macos_helper/openscanner-helper}"
IDENTITY="${IDENTITY:--}"
SIGN=1
DRY_RUN=0

usage() {
  sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
}

ACTION="${1:-install}"
case "$ACTION" in
  install|status|uninstall) shift ;;
  -h|--help|help) usage; exit 0 ;;
  *) echo "install_user_agent: unknown action '$ACTION' (install|status|uninstall)" >&2; exit 2 ;;
esac

while [ $# -gt 0 ]; do
  case "$1" in
    --helper) [ $# -ge 2 ] || { echo "--helper needs a path" >&2; exit 2; }; HELPER="$2"; shift 2 ;;
    --identity) [ $# -ge 2 ] || { echo "--identity needs a name" >&2; exit 2; }; IDENTITY="$2"; shift 2 ;;
    --no-sign) SIGN=0; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    *) echo "install_user_agent: unknown option '$1'" >&2; exit 2 ;;
  esac
done

if [ "$(uname -s)" != "Darwin" ]; then
  echo "install_user_agent: macOS only (this helper is a macOS LaunchAgent)" >&2
  exit 2
fi

UID_NUM="$(id -u)"
DOMAIN="gui/$UID_NUM"

case "$ACTION" in
  status)
    if launchctl print "$DOMAIN/$LABEL" 2>/dev/null; then
      echo
      echo "XPC status (the shim's own claim query):"
      echo "  \"$ROOT/build/tools/raptor_shim/raptor_shim\" --helper-status"
    else
      echo "install_user_agent: $LABEL is not registered in $DOMAIN" >&2
      exit 1
    fi
    ;;

  uninstall)
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
    rm -f "$DEST"
    echo "install_user_agent: removed $LABEL from $DOMAIN and $DEST"
    ;;

  install)
    if [ ! -x "$HELPER" ]; then
      echo "install_user_agent: $HELPER is not an executable — build it first:" >&2
      echo "  cmake --build build --target openscanner-helper raptor_shim" >&2
      exit 2
    fi
    if [ "$DRY_RUN" = 1 ]; then
      DEST="$(mktemp "${TMPDIR:-/tmp}/$LABEL.plist.XXXXXX")"
    elif [ "$SIGN" = 1 ]; then
      "$ROOT/tools/macos_helper/dev_sign.sh" "$HELPER" "$IDENTITY"
    fi
    if [ "$DRY_RUN" != 1 ]; then
      mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
    fi
    # The plist carries only absolute paths; substitute them verbatim.
    sed -e "s|@HELPER_PROGRAM@|$HELPER|g" \
        -e "s|@MACH_SERVICE@|$LABEL|g" \
        -e "s|@HELPER_LOG@|$LOG|g" \
        "$TEMPLATE" > "$DEST"
    plutil -lint "$DEST" >/dev/null
    if [ "$DRY_RUN" = 1 ]; then
      echo "install_user_agent: dry run — plist lints; not registered"
      cat "$DEST"
      rm -f "$DEST"
      exit 0
    fi
    # Replace an older registration, then bootstrap in the user domain. The
    # bootout failure is expected on a first install and is not an error.
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
    launchctl bootstrap "$DOMAIN" "$DEST"
    launchctl kickstart -kp "$DOMAIN/$LABEL" 2>/dev/null || true
    echo "install_user_agent: registered $LABEL in $DOMAIN"
    echo "  plist:  $DEST"
    echo "  log:    $LOG"
    echo "  verify: $0 status"
    ;;
esac
