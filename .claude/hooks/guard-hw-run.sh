#!/usr/bin/env bash
# PreToolUse guard for Bash in the Open3DScanner project.
#
# Exit 0  -> allow the tool call.
# Exit 2  -> block it; stderr is shown to the agent as the reason.
#
# Claude Code passes the tool call as JSON on stdin, including the working
# directory:
#   {"session_id":"...","cwd":"/path","tool_name":"Bash",
#    "tool_input":{"command":"..."}}
set -euo pipefail

input=$(cat)
field() {
  printf '%s' "$input" | python3 -c \
    'import sys,json; print(json.load(sys.stdin).get(sys.argv[1],""))' "$1" 2>/dev/null || true
}
cmd=$(printf '%s' "$input" | python3 -c \
  'import sys,json; print(json.load(sys.stdin).get("tool_input",{}).get("command",""))' 2>/dev/null || true)
cwd=$(field cwd)

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

# 4. The hardware test must run against the interposer build. The main checkout
#    still has the old launcher, so a run there is both unsafe and meaningless.
#    Accept either the session's cwd or an explicit `cd <worktree>` in the
#    command — a bare relative path is only safe when the cwd is the worktree.
case "$cmd" in
  *device_message_listener_test*)
    case "$cwd" in
      *Open3DScanner-wt-hw*) ;;
      *)
        case "$cmd" in
          *Open3DScanner-wt-hw*) ;;
          *) block "run the hardware test only from Open3DScanner-wt-hw (the interposer build)";;
        esac
        ;;
    esac
    ;;
esac

exit 0
