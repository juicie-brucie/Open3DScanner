#!/usr/bin/env bash
# Hardware-free evidence for row #130: the hardware tests must tell "the
# precondition is not met" (skip, exit 2) from "the run failed" (exit 1).
#
#   tests/hardware/precondition_evidence.sh [build-dir]
#
# The vendor library is never loaded here: the tests are pointed at the SDK
# test double (tests/hardware/stub_sdk), and the two precondition verdicts
# are forced through the probe's test-only overrides. Three paths are checked
# per listener test:
#
#   1. the daemon is missing      -> exit 2, and the reason names the daemon
#   2. the device is missing      -> exit 2, and the reason names the device
#   3. both hold, the SDK refuses -> exit 1 (a genuine failure stays 1)
#
# The probe's own verdict on this host is printed too (it loads nothing).
#
# Exit 0 when every path behaved as expected; 1 otherwise.
set -uo pipefail

BUILD_DIR="${1:-build}"
TEST_ROOT="$BUILD_DIR/tests/hardware"
STUB="$TEST_ROOT/stub_sdk/libobscan_stub.dylib"

DAEMON_REASON='precondition not met: CrealityScan (RPC daemon host) is not running'
DEVICE_REASON='precondition not met: no VID_2BC5 device enumerated'

FAILURES=0

note() { printf '%s\n' "$*"; }

fail() {
    printf 'FAIL: %s\n' "$*"
    FAILURES=$((FAILURES + 1))
}

# run_case <label> <want-exit> <want-substring-or-empty> <env-assignments...> -- <command...>
run_case() {
    local label="$1" want_exit="$2" want_text="$3"
    shift 3
    local envs=()
    while [ "$1" != "--" ]; do
        envs+=("$1")
        shift
    done
    shift

    note ""
    note "--- $label ---"
    local env_text=""
    if [ "${#envs[@]}" -gt 0 ]; then env_text="${envs[*]} "; fi
    note "\$ ${env_text}$*"
    local output
    local code
    if [ "${#envs[@]}" -gt 0 ]; then
        output="$(env "${envs[@]}" "$@" 2>&1)"
        code=$?
    else
        output="$("$@" 2>&1)"
        code=$?
    fi
    printf '%s\n' "$output"
    note "exit: $code (want $want_exit)"

    if [ "$code" != "$want_exit" ]; then
        fail "$label: exit $code, want $want_exit"
    fi
    if [ -n "$want_text" ] && ! printf '%s\n' "$output" | grep -qF "$want_text"; then
        fail "$label: output does not contain '$want_text'"
    fi
    if [ -z "$want_text" ] && [ "$want_exit" = "2" ]; then
        fail "$label: a skip must carry a reason line"
    fi
}

for test_name in device_message_listener_test data_process_message_listener_test; do
    test_bin="$TEST_ROOT/$test_name/$test_name"
    if [ ! -x "$test_bin" ]; then
        fail "$test_name: not built ($test_bin) — build first (cmake --build $BUILD_DIR)"
        continue
    fi
    if [ ! -f "$STUB" ]; then
        fail "$test_name: the SDK test double is missing ($STUB) — build first"
        continue
    fi

    run_case "$test_name: probe verdict on this host" 0 "" -- \
        "$test_bin" --precondition-probe

    run_case "$test_name: daemon missing -> skip" 2 "$DAEMON_REASON" \
        OPENSCANNER_HW_DAEMON_STATE=absent -- \
        "$test_bin" "$STUB"

    run_case "$test_name: device missing -> skip" 2 "$DEVICE_REASON" \
        OPENSCANNER_HW_DEVICE_STATE=absent -- \
        "$test_bin" "$STUB"

    run_case "$test_name: probe indeterminate -> skip" 2 "precondition probe failed" \
        OPENSCANNER_HW_DAEMON_STATE=unknown -- \
        "$test_bin" "$STUB"

    run_case "$test_name: SDK refuses to create a context -> failure" 1 "obscan_context_create()" \
        OPENSCANNER_HW_DAEMON_STATE=present OPENSCANNER_HW_DEVICE_STATE=present -- \
        "$test_bin" "$STUB"
done

note ""
if [ "$FAILURES" -eq 0 ]; then
    note "precondition evidence: OK — every path behaved as expected."
    exit 0
fi
note "precondition evidence: $FAILURES check(s) failed."
exit 1
