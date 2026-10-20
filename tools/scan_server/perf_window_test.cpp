// Hardware-free test for the per-tick timing fields (rows #111, #139, #141).
//
// Asserts the schema: the four historical work segments keep indices 0-3 and
// their names; the wait (4), the exposure re-assert (5) and the cloud release
// (6) are separate; overshoot = wait - nominal; and the untimed residual is
// `period - sum(segments)`. The `BeginTickAt` seam makes it deterministic.
#include "perf_window.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace perf = openscanner::perf;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "[FAIL] %s\n", what.c_str());
    ++g_failures;
}

void CheckStableIndicesAndNames() {
    Check(perf::kPose == 0, "kPose keeps index 0");
    Check(perf::kGetter == 1, "kGetter keeps index 1");
    Check(perf::kPostproc == 2, "kPostproc keeps index 2");
    Check(perf::kBroadcast == 3, "kBroadcast keeps index 3");
    Check(perf::kWait == 4, "kWait keeps index 4");
    Check(perf::kExposure == 5 && perf::kRelease == 6, "the vendor calls are appended");
    Check(perf::kSegmentCount == 7, "seven segments");
    Check(std::string(perf::SegmentName(perf::kPose)) == "pose", "pose name");
    Check(std::string(perf::SegmentName(perf::kGetter)) == "getter", "getter name");
    Check(std::string(perf::SegmentName(perf::kPostproc)) == "post", "post name");
    Check(std::string(perf::SegmentName(perf::kBroadcast)) == "bcast", "bcast name");
    Check(std::string(perf::SegmentName(perf::kWait)) == "wait", "wait name");
    Check(std::string(perf::SegmentName(perf::kExposure)) == "exposure", "exposure name");
    Check(std::string(perf::SegmentName(perf::kRelease)) == "release", "release name");
}

void CheckFieldsAndResidual() {
    perf::TickPerf tp;
    const int ticks = 10;
    int64_t now_us = 0;
    for (int i = 0; i < ticks; ++i) {
        tp.BeginTickAt(now_us);
        perf::TickPerf::Sample sample;
        sample.pose_us = 100;
        sample.getter_us = 200;
        sample.post_us = 300;
        sample.broadcast_us = 400;
        sample.wait_us = 130000;      // 100 ms budget + 30 ms
        sample.exposure_us = 5000;    // the previously untimed vendor call
        sample.release_us = 500;      // the other one
        sample.points = 1000;
        sample.bytes = 2000;
        sample.clients = 2;
        sample.nominal_wait_us = 100000;
        tp.EndTick(sample);
        now_us += 140000;  // 140 ms period
    }
    const perf::TickPerf::Snapshot snap = tp.TakeSnapshot();
    const int64_t work = 100 + 200 + 300 + 400 + 130000 + 5000 + 500;  // 136,500 us

    Check(snap.segment[perf::kPose].p50 == 100, "pose p50 unchanged");
    Check(snap.segment[perf::kGetter].p50 == 200, "getter p50 unchanged");
    Check(snap.segment[perf::kPostproc].p50 == 300, "post p50 unchanged");
    Check(snap.segment[perf::kBroadcast].p50 == 400, "bcast p50 unchanged");
    Check(snap.segment[perf::kWait].p50 == 130000, "wait p50 recorded");
    Check(snap.segment[perf::kExposure].p50 == 5000, "exposure p50 recorded");
    Check(snap.segment[perf::kRelease].p50 == 500, "release p50 recorded");
    Check(snap.overshoot.p50 == 30000, "overshoot = wait - nominal");
    Check(snap.tick.p50 == 140000, "tick_total = the realized period");
    Check(snap.residual.p50 == 140000 - work, "residual = period - sum(segments)");
    Check(snap.residual.p50 == 3500, "residual value");
    Check(snap.mean_clients > 1.99 && snap.mean_clients < 2.01, "mean clients");
    Check(snap.nominal_wait_us == 100000, "nominal wait recorded");

    const perf::TickPerf::Snapshot again = tp.TakeSnapshot();
    Check(again.n == snap.n, "TakeSnapshot does not reset the window");
}

void CheckShortWaitIsNegativeOvershoot() {
    perf::TickPerf tp;
    tp.BeginTickAt(0);
    perf::TickPerf::Sample sample;
    sample.wait_us = 90000;
    sample.nominal_wait_us = 100000;
    tp.EndTick(sample);
    const perf::TickPerf::Snapshot snap = tp.TakeSnapshot();
    Check(snap.segment[perf::kWait].p50 == 90000, "short wait recorded");
    Check(snap.overshoot.p50 == -10000, "a short wait is a negative overshoot");
}

}  // namespace

int main() {
    unsetenv("OPENSCANNER_TICK_PERF");
    CheckStableIndicesAndNames();
    CheckFieldsAndResidual();
    CheckShortWaitIsNegativeOvershoot();
    if (g_failures != 0) {
        std::fprintf(stderr, "[perf_window_test] %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("[perf_window_test] PASS: 4 work segments unchanged; wait/exposure/release separate; "
                "overshoot = wait - nominal; residual = period - sum\n");
    return 0;
}
