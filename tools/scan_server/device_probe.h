// Row #167: the live start's device-enumeration wait.
//
// The vendor's device manager can log a device as present ("Current
// device(s) list: (1)") and still return count=0 from the next
// obscan_device_management_get_device_num. Taking the no-device exit on that
// transient reaches the vendor's teardown, which crashes when no device was
// opened — row #167's records carry the runs, the logs and the crash stacks.
//
// The probe and the sleep are passed in, so this timing is testable without
// the SDK or a device (device_probe_test.cpp). The policy it pins: 6 probes
// (one immediate + five retries), 250 ms doubling to a 1500 ms cap — 4.75 s of
// waiting in total, on top of the live start's existing connect-message wait.
// A transient count=0 recovers inside it; a real no-device still ends at the
// documented exit 2 (see RunLiveServer).
#pragma once

#include <algorithm>
#include <cstdint>

#include "openscanner/obscan_api.h"

namespace openscanner::scan_server {

struct DeviceProbePolicy {
    int attempts = 6;
    int first_interval_ms = 250;
    int max_interval_ms = 1500;
};

struct DeviceProbeSample {
    int32_t status = OB_SCAN_ERROR_FATAL;
    int32_t count = 0;
};

struct DeviceProbeOutcome {
    int32_t status = OB_SCAN_ERROR_FATAL;
    int32_t count = 0;
    int attempts = 0;    // probes made, including the first
    int waited_ms = 0;   // time spent between probes

    bool present() const { return status == OB_SCAN_SUCCESS && count > 0; }
};

// The sleep before the next probe: 250, 500, 1000, 1500 (capped).
inline int NextDeviceProbeIntervalMs(int previous_ms, const DeviceProbePolicy& policy) {
    const int cap = std::max(policy.max_interval_ms, 1);
    if (previous_ms < 1) return std::min(std::max(policy.first_interval_ms, 1), cap);
    const int64_t doubled = static_cast<int64_t>(previous_ms) * 2;
    return static_cast<int>(std::min<int64_t>(doubled, cap));
}

// The policy's full sleep budget, for the log line and the test.
inline int DeviceProbeBudgetMs(const DeviceProbePolicy& policy) {
    int total = 0;
    int interval = 0;
    for (int attempt = 1; attempt < std::max(policy.attempts, 1); ++attempt) {
        interval = NextDeviceProbeIntervalMs(interval, policy);
        total += interval;
    }
    return total;
}

// Probes until a device is present or the policy's attempts are used up. The
// first probe is immediate; each miss is followed by one sleep. The last
// sample is what the caller reports either way.
template <typename ProbeFn, typename SleepFn>
DeviceProbeOutcome WaitForDevice(ProbeFn&& probe, SleepFn&& sleep, const DeviceProbePolicy& policy) {
    DeviceProbeOutcome outcome;
    const int attempts = std::max(policy.attempts, 1);
    int interval = 0;
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        const DeviceProbeSample sample = probe();
        outcome.status = sample.status;
        outcome.count = sample.count;
        outcome.attempts = attempt;
        if (outcome.present()) return outcome;
        if (attempt == attempts) break;
        interval = NextDeviceProbeIntervalMs(interval, policy);
        sleep(interval);
        outcome.waited_ms += interval;
    }
    return outcome;
}

}  // namespace openscanner::scan_server
