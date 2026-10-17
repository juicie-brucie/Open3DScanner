#!/usr/bin/env bash
# PreToolUse guard for Bash in the Open3DScanner project.
#
# Exit 0  -> allow the tool call.
# Exit 2  -> block it; stderr is shown to the agent as the reason.
#
# Claude Code passes the tool call as JSON on stdin:
#   {"session_id": "...", "tool_name": "Bash", "tool_input": {"command": "..."}}
set -euo pipefail

input=$(cat)
cmd=$(printf '%s' "$input" | python3 -c \
  'import sys,json; print(json.load(sys.stdin).get("tool_input",{}).get("command",""))' 2>/dev/null || true)

block() {
  printf 'BLOCKED by hw-test guard: %s\n' "$1" >&2
  exit 2
}

# 1. The launcher owns the interposer; setting it by hand double-inserts and
#    makes dyld abort.
case "$cmd" in
  *DYLD_INSERT_LIBRARIES=*) block "never set DYLD_INSERT_LIBRARIES; the test launcher does it";;
esac

# 2. Clean-room wall: no vendor artefacts, no decompiled output, no recon cache.
case "$cmd" in
  *".recon-cache"*|*"libOrbbecSDK"*|*"liborbbec_scan.3.5.3"*)
    block "the hardware agent must not read vendor artefacts";;
esac

# 3. The ABI header is recon-only.
case "$cmd" in
  *"obscan_api.h"*) block "obscan_api.h is recon-only";;
esac

# 4. The hardware test must run from the worktree that carries the interposer
#    build; the main checkout still has the old launcher.
case "$cmd" in
  *device_message_listener_test*)
    case "$cmd" in
      *Open3DScanner-wt-build1*) ;;
      *) block "run the hardware test only from Open3DScanner-wt-build1";;
    esac
    ;;
esac

exit 0
