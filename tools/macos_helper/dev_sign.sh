#!/usr/bin/env bash
# Sign the development helper so launchd will run it (row #97's decision:
# self-signed for development, no notarization; row #170).
#
# Ad-hoc signing is enough for a local development daemon and needs no
# Keychain identity. A named self-signed identity works the same way and gives
# a stable designated requirement; pass it as the second argument.
#
#   tools/macos_helper/dev_sign.sh build/tools/macos_helper/openscanner-helper
#   tools/macos_helper/dev_sign.sh <binary> "OpenScanner Dev"
set -euo pipefail

BIN="${1:-}"
IDENTITY="${2:--}"
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
  echo "usage: $0 <path to openscanner-helper> [codesign identity]" >&2
  exit 2
fi

codesign --force --sign "$IDENTITY" --identifier org.openscanner.helper "$BIN"
echo "signed with identity: $IDENTITY"
echo "designated requirement:"
codesign --display --requirements - "$BIN" 2>&1 | sed -n '2,4p'
