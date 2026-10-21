// Row #167's device-free test: the live start's bounded device-enumeration
// wait. The probe and the sleep are injected, so this needs no SDK and no
// device — it pins the policy (attempts, backoff, budget), the transient
// recovery the row is about, and the exhausted path the caller must report.
#include "device_probe.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using openscanner::scan_server::DeviceProbeOutcome;
using openscanner::scan_server::DeviceProbePolicy;
using openscanner::scan_server::DeviceProbeSample;
using openscanner::scan_server::DeviceProbeBudgetMs;
using openscanner::scan_server::NextDeviceProbeIntervalMs;
using openscanner::scan_server::WaitForDevice;

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("[device-probe] %-58s %s\n", what.c_str(), ok ? "PASS" : "FAIL");
    if (!ok) ++g_failures;
}

// A scripted count source: one sample per probe, the last one repeats.
struct ScriptedProbe {
    std::vector<int32_t> counts;
    std::vector<int32_t> statuses;
    size_t calls = 0;

    DeviceProbeSample operator()() {
        const size_t i = calls < counts.size() ? calls : counts.size() - 1;
        ++calls;
        return DeviceProbeSample{statuses[i], counts[i]};
    }
};

}  // namespace

int main() {
    const DeviceProbePolicy policy;

    // --- the policy the PR states ------------------------------------------
    Check(policy.attempts == 6, "policy: 6 probes (1 immediate + 5 retries)");
    Check(policy.first_interval_ms == 250, "policy: first interval 250 ms");
    Check(policy.max_interval_ms == 1500, "policy: backoff capped at 1500 ms");
    Check(DeviceProbeBudgetMs(policy) == 4750, "policy: 4.75 s total sleep budget");

    Check(NextDeviceProbeIntervalMs(0, policy) == 250, "backoff: 0 -> 250 ms");
    Check(NextDeviceProbeIntervalMs(250, policy) == 500, "backoff: 250 -> 500 ms");
    Check(NextDeviceProbeIntervalMs(500, policy) == 1000, "backoff: 500 -> 1000 ms");
    Check(NextDeviceProbeIntervalMs(1000, policy) == 1500, "backoff: 1000 -> cap 1500 ms");
    Check(NextDeviceProbeIntervalMs(1500, policy) == 1500, "backoff: stays at the cap");

    // --- the row #167 case: one transient count=0, then the device ----------
    {
        ScriptedProbe probe{{0, 1}, {OB_SCAN_SUCCESS, OB_SCAN_SUCCESS}};
        std::vector<int> sleeps;
        const DeviceProbeOutcome out = WaitForDevice(
            probe, [&sleeps](int ms) { sleeps.push_back(ms); }, policy);
        Check(out.present(), "transient count=0 then 1 -> device present");
        Check(out.attempts == 2, "transient: recovered on the second probe");
        Check(out.waited_ms == 250, "transient: waited one 250 ms backoff");
        Check(sleeps.size() == 1 && sleeps[0] == 250, "transient: slept before retrying, not after");
        Check(out.count == 1 && out.status == OB_SCAN_SUCCESS, "transient: outcome carries the live sample");
    }

    {
        ScriptedProbe probe{{0, 0, 1}, {OB_SCAN_SUCCESS, OB_SCAN_SUCCESS, OB_SCAN_SUCCESS}};
        const DeviceProbeOutcome out = WaitForDevice(probe, [](int) {}, policy);
        Check(out.present() && out.attempts == 3, "three probes: count=0,0,1 -> present on the third");
        Check(out.waited_ms == 750, "three probes: 250 + 500 ms of backoff");
    }

    // --- a persistent no-device exhausts the budget and reports it ----------
    {
        ScriptedProbe probe{{0}, {OB_SCAN_SUCCESS}};
        int slept = 0;
        const DeviceProbeOutcome out = WaitForDevice(
            probe, [&slept](int ms) { slept += ms; }, policy);
        Check(!out.present(), "persistent count=0 -> not present");
        Check(out.attempts == policy.attempts, "persistent: every policy attempt used");
        Check(probe.calls == static_cast<size_t>(policy.attempts), "persistent: probe called once per attempt");
        Check(out.waited_ms == DeviceProbeBudgetMs(policy), "persistent: full budget waited");
        Check(slept == DeviceProbeBudgetMs(policy), "persistent: sleep total matches the budget");
        Check(out.count == 0, "persistent: last count reported");
    }

    // --- a failing status is retried on the same budget, not treated as 0 ---
    {
        ScriptedProbe probe{{0, 1}, {OB_SCAN_ERROR_DEVICE_DISCONNECT, OB_SCAN_SUCCESS}};
        const DeviceProbeOutcome out = WaitForDevice(probe, [](int) {}, policy);
        Check(out.present() && out.attempts == 2, "non-success status is retried too");
    }

    // --- count > 0 with a failing status is not "present" -------------------
    {
        ScriptedProbe probe{{1}, {OB_SCAN_ERROR_DEVICE_DISCONNECT}};
        const DeviceProbeOutcome out = WaitForDevice(probe, [](int) {}, policy);
        Check(!out.present(), "count>0 with a failing status does not proceed");
        Check(out.attempts == policy.attempts, "that case also uses the full budget");
    }

    // --- a zero/negative policy still probes once --------------------------
    {
        DeviceProbePolicy degenerate;
        degenerate.attempts = 0;
        ScriptedProbe probe{{0}, {OB_SCAN_SUCCESS}};
        const DeviceProbeOutcome out = WaitForDevice(probe, [](int) {}, degenerate);
        Check(out.attempts == 1, "degenerate policy: at least one probe");
        Check(out.waited_ms == 0, "degenerate policy: no sleep");
    }

    if (g_failures == 0) {
        std::printf("[device-probe] ok (0 failure(s))\n");
        return 0;
    }
    std::printf("[device-probe] FAILED (%d failure(s))\n", g_failures);
    return 1;
}
