// Hardware-free test for the per-tick timing fields (rows #111, #139, #141).
//
// Asserts the schema: the four historical work segments keep indices 0-3 and
// their names; the wait (4), the exposure re-assert (5) and the cloud release
// (6) are separate; overshoot = wait - nominal; tick_overshoot = tick - nominal;
// and the untimed residual is `period - sum(segments)`. The `BeginTickAt` seam
// makes it deterministic (a fake clock: the test supplies each tick's start).
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
    Check(snap.tick_overshoot.p50 == 40000, "tick_overshoot = tick - nominal");
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
    // A 90 ms period against a 100 ms pacing budget: the whole tick is early.
    tp.BeginTickAt(90000);
    const perf::TickPerf::Snapshot snap = tp.TakeSnapshot();
    Check(snap.segment[perf::kWait].p50 == 90000, "short wait recorded");
    Check(snap.overshoot.p50 == -10000, "a short wait is a negative overshoot");
    Check(snap.tick_overshoot.p50 == -10000, "a short tick is a negative tick_overshoot");
}

// Row #141: a spike that moves no segment must be visible. Ten steady ticks,
// then one 500 ms period with the same work: no segment changes, but
// tick_overshoot and the residual carry the spike.
void CheckSpikeMovesNoSegment() {
    perf::TickPerf tp;
    int64_t now_us = 0;
    for (int i = 0; i < 10; ++i) {
        tp.BeginTickAt(now_us);
        perf::TickPerf::Sample sample;
        sample.pose_us = 100;
        sample.getter_us = 200;
        sample.post_us = 300;
        sample.broadcast_us = 400;
        sample.wait_us = 90000;
        sample.nominal_wait_us = 100000;
        tp.EndTick(sample);
        now_us += 92000;
    }
    // The next tick starts 500 ms after the last one, with the same work.
    tp.BeginTickAt(now_us + 408000);
    const perf::TickPerf::Snapshot snap = tp.TakeSnapshot();
    Check(snap.segment[perf::kWait].max == 90000, "the spike moves no wait");
    Check(snap.segment[perf::kPostproc].max == 300, "the spike moves no work segment");
    Check(snap.tick.max == 500000, "the spike is in the realized tick");
    Check(snap.tick_overshoot.max == 400000, "tick - nominal exposes the spike");
    Check(snap.residual.max == 409000, "the untimed residual exposes the spike");
}

// Row #153: the window counts how many times the exposure config was pushed,
// so the skip is visible in the [perf] line.
void CheckExposureApplyCount() {
    perf::TickPerf tp;
    int64_t now_us = 0;
    for (int i = 0; i < 10; ++i) {
        tp.BeginTickAt(now_us);
        perf::TickPerf::Sample sample;
        if (i == 0) {
            sample.exposure_us = 900;  // the one push in this window
            sample.exposure_applies = 1;
        }
        sample.nominal_wait_us = 100000;
        tp.EndTick(sample);
        now_us += 140000;
    }
    const perf::TickPerf::Snapshot snap = tp.TakeSnapshot();
    Check(snap.exposure_applies == 1, "the window counts the exposure pushes");
    Check(snap.segment[perf::kExposure].max == 900, "the exposure segment still measures the push");
    Check(snap.segment[perf::kExposure].p50 == 0, "the ticks that skipped it measure zero");
}

// Row #163: the broadcast counters are cumulative; the tick's delta must
// subtract the counters and only carry a max/peak when it advanced.
void CheckBroadcastDeltas() {
    perf::BroadcastCumulative previous;
    previous.sends = 10;
    previous.bytes = 1000;
    previous.send_us_total = 500;
    previous.send_us_max = 80;
    previous.drops = 1;
    previous.timeouts = 1;
    previous.queue_depth_max = 2;

    perf::BroadcastCumulative now = previous;
    now.sends += 8;
    now.bytes += 800;
    now.send_us_total += 400;
    now.drops += 1;
    now.timeouts += 0;  // a non-timeout drop
    const perf::BroadcastDelta no_new_max = perf::DeltaBetween(previous, now);
    Check(no_new_max.sends == 8, "the delta subtracts the send count");
    Check(no_new_max.bytes == 800, "the delta subtracts the bytes");
    Check(no_new_max.send_us_total == 400, "the delta subtracts the send time total");
    Check(no_new_max.send_us_max == 0, "no new maximum -> 0, not the stale maximum");
    Check(no_new_max.drops == 1, "the drop delta");
    Check(no_new_max.timeouts == 0, "the timeout delta");
    Check(no_new_max.queue_max == 0, "no new queue peak -> 0");

    now.send_us_max = 90;
    now.queue_depth_max = 3;
    const perf::BroadcastDelta with_max = perf::DeltaBetween(previous, now);
    Check(with_max.send_us_max == 90, "a new lifetime maximum is carried");
    Check(with_max.queue_max == 3, "a new in-flight peak is carried");

    // The same values flow into the window snapshot.
    perf::TickPerf tp;
    int64_t now_us = 0;
    for (int i = 0; i < 5; ++i) {
        tp.BeginTickAt(now_us);
        perf::TickPerf::Sample sample;
        sample.nominal_wait_us = 100000;
        sample.bcast_sends = 8;
        sample.bcast_send_us_total = 160;
        sample.bcast_send_us_max = (i == 2) ? 40 : 0;
        sample.bcast_drops = (i == 3) ? 1 : 0;
        sample.bcast_timeouts = (i == 3) ? 1 : 0;
        sample.bcast_queue_max = (i == 1) ? 1 : 0;
        tp.EndTick(sample);
        now_us += 140000;
    }
    const perf::TickPerf::Snapshot snap = tp.TakeSnapshot();
    Check(snap.bcast_sends == 40, "the window sums the per-client sends");
    Check(snap.bcast_send_us_total == 800, "the window sums the send time");
    Check(snap.bcast_send_us_max == 40, "the window keeps the biggest new max");
    Check(snap.bcast_drops == 1, "the window counts the drops");
    Check(snap.bcast_timeouts == 1, "the window counts the timeouts");
    Check(snap.bcast_queue_max == 1, "the window keeps the in-flight peak");
}

}  // namespace

int main() {
    unsetenv("OPENSCANNER_TICK_PERF");
    CheckStableIndicesAndNames();
    CheckFieldsAndResidual();
    CheckShortWaitIsNegativeOvershoot();
    CheckSpikeMovesNoSegment();
    CheckExposureApplyCount();
    CheckBroadcastDeltas();
    if (g_failures != 0) {
        std::fprintf(stderr, "[perf_window_test] %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("[perf_window_test] PASS: 4 work segments unchanged; wait/exposure/release separate; "
                "overshoot = wait - nominal; tick_overshoot = tick - nominal; residual = period - sum; "
                "a spike that moves no segment is visible; the exposure pushes are counted; "
                "the broadcast deltas subtract and carry new maxima only\n");
    return 0;
}
