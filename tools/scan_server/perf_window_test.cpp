// Hardware-free test for the per-tick timing fields (rows #111, #139).
//
// Asserts that the four historical work segments keep their indices and names,
// that the pacing wait is recorded as its own segment, and that the overshoot
// distribution is `wait - nominal`. A regression that folds the wait back into
// the tick (or drops it) fails here.
#include "perf_window.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    Check(perf::kWait == 4 && perf::kSegmentCount == 5, "the wait is appended after the four");
    Check(std::string(perf::SegmentName(perf::kPose)) == "pose", "pose name");
    Check(std::string(perf::SegmentName(perf::kGetter)) == "getter", "getter name");
    Check(std::string(perf::SegmentName(perf::kPostproc)) == "post", "post name");
    Check(std::string(perf::SegmentName(perf::kBroadcast)) == "bcast", "bcast name");
    Check(std::string(perf::SegmentName(perf::kWait)) == "wait", "wait name");
}

void CheckWaitAndOvershoot() {
    perf::TickPerf tp;
    const int ticks = 10;
    for (int i = 0; i < ticks; ++i) {
        tp.BeginTick();
        // 100/200/300/400 us of work; a 130 ms wait against a 100 ms budget.
        tp.EndTick(100, 200, 300, 400, 130000, 1000, 2000, 100000);
    }
    const perf::TickPerf::Snapshot snap = tp.TakeSnapshot();
    Check(snap.segment[perf::kPose].p50 == 100, "pose p50 unchanged");
    Check(snap.segment[perf::kGetter].p50 == 200, "getter p50 unchanged");
    Check(snap.segment[perf::kPostproc].p50 == 300, "post p50 unchanged");
    Check(snap.segment[perf::kBroadcast].p50 == 400, "bcast p50 unchanged");
    Check(snap.segment[perf::kWait].p50 == 130000, "wait p50 recorded");
    Check(snap.segment[perf::kWait].p95 == 130000, "wait p95 recorded");
    Check(snap.segment[perf::kWait].max == 130000, "wait max recorded");
    Check(snap.overshoot.p50 == 30000, "overshoot p50 = wait - nominal");
    Check(snap.overshoot.p95 == 30000 && snap.overshoot.max == 30000, "overshoot p95/max");
    Check(snap.n == ticks - 1, "n counts period samples");
    Check(snap.mean_points > 999.0 && snap.mean_points < 1001.0, "mean points preserved");

    const perf::TickPerf::Snapshot again = tp.TakeSnapshot();
    Check(again.n == snap.n, "TakeSnapshot does not reset the window");

    // The four work segments must not absorb the wait: their sum is still the
    // work, ~1 ms, not the ~130 ms tick.
    const int64_t work_p50 = snap.segment[perf::kPose].p50 + snap.segment[perf::kGetter].p50 +
                             snap.segment[perf::kPostproc].p50 + snap.segment[perf::kBroadcast].p50;
    Check(work_p50 == 1000, "the work sum stays the four segments");
    Check(snap.segment[perf::kWait].p50 > work_p50, "the wait is the dominant component, separately");
}

void CheckShortWaitIsNegativeOvershoot() {
    perf::TickPerf tp;
    tp.BeginTick();
    tp.EndTick(1, 2, 3, 4, 90000, 0, 0, 100000);
    const perf::TickPerf::Snapshot snap = tp.TakeSnapshot();
    Check(snap.segment[perf::kWait].p50 == 90000, "short wait recorded");
    Check(snap.overshoot.p50 == -10000, "a short wait is a negative overshoot");
}

}  // namespace

int main() {
    // The instrumentation is env-switchable; the test needs it on.
    unsetenv("OPENSCANNER_TICK_PERF");
    CheckStableIndicesAndNames();
    CheckWaitAndOvershoot();
    CheckShortWaitIsNegativeOvershoot();
    if (g_failures != 0) {
        std::fprintf(stderr, "[perf_window_test] %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("[perf_window_test] PASS: four work segments unchanged; wait recorded separately; "
                "overshoot = wait - nominal\n");
    return 0;
}
