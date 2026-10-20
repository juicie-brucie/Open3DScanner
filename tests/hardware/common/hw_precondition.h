#pragma once

#include <string>

namespace hwtest {

// A hardware test returns this when a precondition is not met. It is the same
// value the hardware tests declare as CTest's SKIP_RETURN_CODE, so "the run
// could not happen" is recorded as a skip and not as a failure.
constexpr int kPreconditionSkipExitCode = 2;

// What one precondition probe can answer.
enum class PreconditionState {
    kSatisfied,      // the precondition holds
    kMissing,        // it does not hold
    kIndeterminate,  // the probe could not tell
};

// The outcome of probing the preconditions a hardware run needs.
//
// The reason lines are a contract with the Windows lane (the cross-machine
// grep token is "(RPC daemon host) is not running"; the device wording is
// literal on both sides). See docs/recomp/windows-handover-order.md, "SKIP
// wording when a precondition is not met".
struct PreconditionReport {
    PreconditionState daemon = PreconditionState::kIndeterminate;
    PreconditionState device = PreconditionState::kIndeterminate;
    // One word each for the probe's own state, for the "probe failed" reason.
    std::string daemon_detail;
    std::string device_detail;
    // The skip reason; empty when both preconditions hold.
    std::string reason;

    bool satisfied() const {
        return daemon == PreconditionState::kSatisfied && device == PreconditionState::kSatisfied;
    }
};

// The name the skip reason gives the vendor's RPC daemon host.
const char* DaemonHostName();

// Probes the daemon and the device and builds the skip reason.
//
// `checks_label` names what a skip would not exercise ("listener checks").
//
// Two environment variables override what a probe REPORTS, so the skip path
// can be exercised on a host with no daemon and no device. They never change
// what the SDK does:
//   OPENSCANNER_HW_DAEMON_STATE = present | absent | unknown
//   OPENSCANNER_HW_DEVICE_STATE = present | absent | unknown
PreconditionReport ProbePreconditions(const std::string& checks_label);

// Prints the probe result and returns the exit code it implies: 0 when both
// preconditions hold, kPreconditionSkipExitCode otherwise (with the reason on
// stdout). Used by the --precondition-probe mode of the hardware tests.
int RunPreconditionProbeMode(const std::string& checks_label);

}  // namespace hwtest
