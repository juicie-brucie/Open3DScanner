#!/usr/bin/env bash
# hw-preflight.sh — mechanical pre-run gate for the hw-test agent.
#
#   scripts/hw-preflight.sh <target> [expected-worktree] [binary-path]
#
# <target> is the program under test: scan_server | raptor_client |
# device_message_listener_test | data_process_message_listener_test | ...
#
# Exit 0  -> all checks pass; a pre-filled record header block is printed to stdout.
# Exit 1  -> a check failed; the reason is on stderr. The agent STOPS and reports it.
#
# Folds hw-test rules 1, 7, 12, 13 (+ the interposer expectation) into one gate so
# the two most common correction causes — a stale binary (#107) and a bad process
# check (#13) — are code, not memory.
#
# Two determinism rules, because both caused real misreads:
#   * BINARY SELECTION is a fixed order, never `find | head -1`: the explicit
#     <binary-path> if given, else the build-tree copy build/tools/<target>/<target>,
#     else the .app bundle copy build/**/Contents/MacOS/<target>. It prints which
#     one it chose and reports the other copy's mtime when both exist — the bundle
#     copy is refreshed only on an app build (#107).
#   * FRESHNESS compares the binary against the NEWEST source in the target's
#     directory (*.cpp, *.h, *.mm) — a header-only change counts (#89, #111).
set -euo pipefail

TARGET="${1:-}"
EXPECTED_WT="${2:-${HW_EXPECTED_WT:-}}"
EXPLICIT_BIN="${3:-}"
die() { printf 'hw-preflight: FAIL — %s\n' "$1" >&2; exit 1; }
[ -n "$TARGET" ] || die "usage: hw-preflight.sh <target> [expected-worktree] [binary-path]"

# 1. Worktree + branch (rule 1). The hw worktree is the unique *-wt-hw worktree
#    unless the caller names one; no hard-coded home directory.
if [ -z "$EXPECTED_WT" ]; then
  EXPECTED_WT="$(git worktree list --porcelain 2>/dev/null \
    | awk '/^worktree /{print $2}' | grep -E -- '-wt-hw$' | head -1 || true)"
fi
[ -n "$EXPECTED_WT" ] || die "cannot find an hw worktree (*-wt-hw); pass it as the second argument or set HW_EXPECTED_WT."
WT_REAL="$(cd "$EXPECTED_WT" 2>/dev/null && pwd -P || true)"
[ -n "$WT_REAL" ] || die "expected worktree '$EXPECTED_WT' does not exist."
[ "$(pwd -P)" = "$WT_REAL" ] || die "pwd is '$(pwd -P)', expected the hw worktree '$WT_REAL'. cd there first."
BRANCH="$(git branch --show-current 2>/dev/null || echo '?')"

# 2. Locate the binary actually under test — fixed order (see header).
BUILD_COPY="build/tools/$TARGET/$TARGET"
BUNDLE_COPY="$(find build -type f -perm -u+x -path "*/Contents/MacOS/$TARGET" 2>/dev/null | sort | head -1)"
if [ -n "$EXPLICIT_BIN" ]; then
  BIN="$EXPLICIT_BIN"; WHY="explicit argument (the binary this run will execute)"
elif [ -x "$BUILD_COPY" ]; then
  BIN="$BUILD_COPY"; WHY="build-tree copy (fixed order: build/tools/<target>/<target>)"
elif [ -n "$BUNDLE_COPY" ] && [ -x "$BUNDLE_COPY" ]; then
  BIN="$BUNDLE_COPY"; WHY=".app bundle copy (no build-tree copy exists)"
else
  die "no built binary named '$TARGET': looked for $BUILD_COPY and */Contents/MacOS/$TARGET — build it first."
fi
[ -x "$BIN" ] || die "binary '$BIN' is not executable."
ALT=""
if [ -x "$BUILD_COPY" ] && [ "$BIN" != "$BUILD_COPY" ]; then
  ALT="other copy: $BUILD_COPY ($(stat -f '%Sm' -t '%H:%M:%S' "$BUILD_COPY"))"
elif [ -n "$BUNDLE_COPY" ] && [ -x "$BUNDLE_COPY" ] && [ "$BIN" != "$BUNDLE_COPY" ]; then
  ALT="other copy: $BUNDLE_COPY ($(stat -f '%Sm' -t '%H:%M:%S' "$BUNDLE_COPY")) — refreshed only on an app build (#107)"
fi

# 3. Binary-vs-source freshness against the NEWEST source in the target dir.
SRC_DIR="$(find tools tests engine -type d -name "$TARGET" 2>/dev/null | sort | head -1)"
NEWEST_SRC=""; NEWEST_MT=0
if [ -n "$SRC_DIR" ]; then
  while IFS= read -r f; do
    m="$(stat -f '%m' "$f" 2>/dev/null || echo 0)"
    if [ "$m" -gt "$NEWEST_MT" ]; then NEWEST_MT="$m"; NEWEST_SRC="$f"; fi
  done < <(find "$SRC_DIR" -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) 2>/dev/null | sort)
fi
if [ -n "$NEWEST_SRC" ]; then
  BIN_MT="$(stat -f '%m' "$BIN" 2>/dev/null || echo 0)"
  [ "$NEWEST_MT" -le "$BIN_MT" ] || die "newest source '$NEWEST_SRC' is NEWER than binary '$BIN' — rebuild (a header change counts; #107: a worker-only rebuild can leave a stale bundle copy)."
  FRESH="binary $(stat -f '%Sm' -t '%H:%M:%S' "$BIN") >= newest source $(basename "$NEWEST_SRC") $(stat -f '%Sm' -t '%H:%M:%S' "$NEWEST_SRC")"
else
  FRESH="source dir not auto-found; verify freshness by hand if in doubt"
fi

# 4. Name-anchored conflicting-process check (rule 7 / rule 13).
#    pgrep -x matches the executable name exactly — never a substring of a path.
CONFLICT=""
for p in scan_server raptor_client device_message_listener_test data_process_message_listener_test live_capture_test capture_pipeline_smoke_test; do
  if pgrep -x "$p" >/dev/null 2>&1; then CONFLICT="$CONFLICT $p($(pgrep -x "$p" | tr '\n' ','))"; fi
done
[ -z "$CONFLICT" ] || die "a conflicting SDK client is already running:$CONFLICT — one run at a time."

# 5. Interposer expectation for the record (rule 6). raptor_client links no vendor SDK.
case "$TARGET" in
  raptor_client) INTERP="n/a — links no vendor SDK (socket/STL only); vendor-artifact & interposer rules do not apply" ;;
  scan_server)   INTERP="expect line 2: [vulkan_disable_interpose] loaded  (scan_server sets DYLD_INSERT_LIBRARIES itself)" ;;
  *)             INTERP="expect first line: [launcher] DYLD_INSERT_LIBRARIES=...libMoltenVK.dylib:...libvulkan_disable_interpose.dylib" ;;
esac

# --- all checks passed: emit the pre-filled record header ---
cat <<EOF
hw-preflight: OK

----8<---- paste into the record ----8<----
- Date: $(date +%Y-%m-%d)
- Worktree: $WT_REAL
- Branch: $BRANCH
- Target: $BIN  ($WHY)
- Other copy: ${ALT:-none}
- Preflight: pwd OK; no conflicting process; freshness: $FRESH
- Interposer: $INTERP
- Run start: $(date '+%Y-%m-%d %H:%M:%S')   # pass this and '$TARGET' to hw-postflight.sh
----8<----------------------------------8<----
EOF
