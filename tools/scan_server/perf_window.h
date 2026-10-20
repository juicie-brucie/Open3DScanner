// Per-tick latency instrumentation (row #111). Measurement only: it times the
// existing operations and never changes what they are, their order, or the
// cadence. The hardware baseline is #112.
//
// Per tick it accumulates four segments — pose, the publish getter, our
// post-processing + encode, and the broadcast — plus the realized tick period
// and the cloud frame's points/bytes. A `[perf] window` line is printed every
// 5 s (the data must survive a teardown that can hang, #89), with a cumulative
// summary at teardown.
//
// No allocation on the timing path: SegTimer is one steady_clock pair into a
// caller-owned int64; samples land in fixed arrays. The percentile scratch is a
// stack array at flush time only.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace openscanner::perf {

// The four work segments keep their indices and meaning (#111); the pacing wait
// is appended so historical numbers stay comparable (row #139).
enum Segment : int {
    kPose = 0, kGetter = 1, kPostproc = 2, kBroadcast = 3,  // #111: unchanged
    kWait = 4,                                              // #139: pacing wait
    kExposure = 5, kRelease = 6,                            // #141: the untimed vendor calls
    kSegmentCount = 7
};

inline const char* SegmentName(int segment) {
    switch (segment) {
        case kPose: return "pose";
        case kGetter: return "getter";
        case kPostproc: return "post";
        case kBroadcast: return "bcast";
        case kWait: return "wait";
        case kExposure: return "exposure";
        case kRelease: return "release";
        default: return "?";
    }
}

inline int64_t UsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
}

// One steady_clock pair per scope, accumulating into a caller-owned slot.
class SegTimer {
public:
    explicit SegTimer(int64_t* acc) : acc_(acc), start_(std::chrono::steady_clock::now()) {}
    ~SegTimer() { Stop(); }
    // End the timed scope early (the caller may need the rest of the block
    // untimed, e.g. an operation that has its own segment).
    void Stop() {
        if (acc_ == nullptr) return;
        *acc_ += UsSince(start_);
        acc_ = nullptr;
    }
    SegTimer(const SegTimer&) = delete;
    SegTimer& operator=(const SegTimer&) = delete;

private:
    int64_t* acc_;
    std::chrono::steady_clock::time_point start_;
};

class TickPerf {
public:
    static constexpr int kCapacity = 512;        // > one 5 s window at 10 Hz
    static constexpr int kWindowMs = 5000;

    // Default on for the baseline; OPENSCANNER_TICK_PERF=0 disables it.
    TickPerf() {
        const char* v = std::getenv("OPENSCANNER_TICK_PERF");
        enabled_ = !(v != nullptr && v[0] == '0' && v[1] == '\0');
    }

    bool enabled() const { return enabled_; }

    static int64_t SteadyNowUs() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    // Call at the very top of the tick: the realized period is start-to-start.
    void BeginTick() { BeginTickAt(SteadyNowUs()); }

    // The `...At` form is the deterministic seam: the test supplies the clock.
    void BeginTickAt(int64_t now_us) {
        if (!enabled_) return;
        if (have_last_) {
            const int64_t period_us = now_us - last_us_;
            Push(periods_, period_n_, period_us);
            // #141: the untimed residual for the tick that just ended.
            const int64_t residual = period_us - pending_work_us_;
            Push(residual_us_, residual_n_, residual);
            cumulative_residual_us_ += residual;
        }
        last_us_ = now_us;
        have_last_ = true;
        if (!flush_started_) {
            flush_start_us_ = now_us;
            flush_started_ = true;
        }
    }

    // One tick's measurement. All times are steady_clock microseconds; the
    // window/line reports ms. `nominal_wait_us` is the pacing budget, so the
    // overshoot is `wait_us - nominal_wait_us` (#139).
    struct Sample {
        int64_t pose_us = 0;
        int64_t getter_us = 0;
        int64_t post_us = 0;
        int64_t broadcast_us = 0;
        int64_t wait_us = 0;
        int64_t exposure_us = 0;   // obscan_scan_config_combined_exposure_and_laser_current
        int64_t release_us = 0;    // obscan_scan_model_clouds_release
        uint32_t exposure_applies = 0;  // how many times the exposure config was pushed this tick
        uint32_t points = 0;
        uint64_t bytes = 0;
        uint32_t clients = 0;      // connected at broadcast time (0 = no send)
        int64_t nominal_wait_us = 0;
    };

    // Call once at the end of the tick.
    void EndTick(const Sample& sample) {
        if (!enabled_) return;
        const int64_t values[kSegmentCount] = {sample.pose_us,       sample.getter_us,  sample.post_us,
                                                sample.broadcast_us, sample.wait_us,    sample.exposure_us,
                                                sample.release_us};
        int64_t work_us = 0;
        for (int s = 0; s < kSegmentCount; ++s) {
            Push(samples_[s], counts_[s], values[s]);
            cumulative_us_[s] += values[s];
            if (values[s] > max_us_[s]) max_us_[s] = values[s];
            work_us += values[s];
        }
        pending_work_us_ = work_us;
        last_nominal_wait_us_ = sample.nominal_wait_us;
        const int64_t overshoot = sample.wait_us - sample.nominal_wait_us;
        Push(overshoot_us_, overshoot_n_, overshoot);
        cumulative_overshoot_us_ += overshoot;
        if (overshoot > max_overshoot_us_) max_overshoot_us_ = overshoot;
        Push(points_, points_n_, static_cast<int64_t>(sample.points));
        Push(bytes_, bytes_n_, static_cast<int64_t>(sample.bytes));
        Push(clients_, clients_n_, static_cast<int64_t>(sample.clients));
        exposure_applies_ += sample.exposure_applies;
        cumulative_exposure_applies_ += sample.exposure_applies;
        ++ticks_;
        cumulative_points_ += sample.points;
        cumulative_bytes_ += sample.bytes;
        MaybeFlush();
    }

    struct Percentiles {
        int64_t p50 = 0;
        int64_t p95 = 0;
        int64_t max = 0;
    };
    struct Snapshot {
        int n = 0;
        double window_s = 0.0;
        double rate = 0.0;
        double mean_points = 0.0;
        double mean_bytes = 0.0;
        Percentiles segment[kSegmentCount];
        Percentiles overshoot;
        Percentiles residual;
        Percentiles tick;
        double mean_clients = 0.0;
        int64_t nominal_wait_us = 0;
        uint32_t exposure_applies = 0;  // pushes in the window (row #153)
    };

    // Compute the current window without printing or resetting (used by the
    // hardware-free test so the wait/overshoot fields are assertable).
    Snapshot TakeSnapshot() const {
        Snapshot snap;
        snap.n = period_n_;
        snap.window_s = static_cast<double>(last_us_ - flush_start_us_) / 1e6;
        const double mean_period_us = period_n_ > 0 ? static_cast<double>(Sum(periods_, period_n_)) / period_n_ : 0.0;
        snap.rate = mean_period_us > 0 ? 1e6 / mean_period_us : 0.0;
        snap.mean_points = points_n_ > 0 ? static_cast<double>(Sum(points_, points_n_)) / points_n_ : 0.0;
        snap.mean_bytes = bytes_n_ > 0 ? static_cast<double>(Sum(bytes_, bytes_n_)) / bytes_n_ : 0.0;
        for (int s = 0; s < kSegmentCount; ++s) snap.segment[s] = PercentilesOf(samples_[s], counts_[s]);
        snap.overshoot = PercentilesOf(overshoot_us_, overshoot_n_);
        snap.residual = PercentilesOf(residual_us_, residual_n_);
        snap.tick = PercentilesOf(periods_, period_n_);
        snap.mean_clients = clients_n_ > 0 ? static_cast<double>(Sum(clients_, clients_n_)) / clients_n_ : 0.0;
        snap.nominal_wait_us = last_nominal_wait_us_;
        snap.exposure_applies = exposure_applies_;
        return snap;
    }

    // Teardown: flush any remaining window, then a cumulative summary.
    void Summary() {
        if (!enabled_) return;
        Flush("final");
        if (ticks_ == 0) return;
        std::printf("[perf] summary ticks=%lld mean_points=%.1f mean_bytes=%.1f",
                    static_cast<long long>(ticks_),
                    static_cast<double>(cumulative_points_) / ticks_,
                    static_cast<double>(cumulative_bytes_) / ticks_);
        for (int s = 0; s < kSegmentCount; ++s) {
            std::printf(" %s_mean=%.2fms %s_max=%.2fms", SegmentName(s),
                        static_cast<double>(cumulative_us_[s]) / ticks_ / 1000.0, SegmentName(s),
                        static_cast<double>(max_us_[s]) / 1000.0);
        }
        std::printf(" overshoot_mean=%.2fms overshoot_max=%.2fms%s",
                    static_cast<double>(cumulative_overshoot_us_) / ticks_ / 1000.0,
                    static_cast<double>(max_overshoot_us_) / 1000.0,
                    " (wait - nominal; positive = the pacing call ran long)");
        std::printf(" residual_mean=%.2fms", static_cast<double>(cumulative_residual_us_) / ticks_ / 1000.0);
        std::printf(" exposure_applies_total=%lld", static_cast<long long>(cumulative_exposure_applies_));
        std::printf("\n");
        std::fflush(stdout);
    }

private:
    void MaybeFlush() {
        // Use the tick clock (`last_us_`) so the `BeginTickAt` seam is honored;
        // in production `last_us_` is "now" as of BeginTick.
        if ((last_us_ - flush_start_us_) / 1000 < kWindowMs) return;
        Flush("window");
        flush_start_us_ = last_us_;
    }

    void Flush(const char* tag) {
        if (period_n_ == 0 && counts_[kPose] == 0) return;
        const Snapshot snap = TakeSnapshot();
        std::printf("[perf] %s %.1fs clock=steady_clock n=%d rate=%.1f/s points=%.1f bytes=%.1f", tag,
                    snap.window_s, snap.n, snap.rate, snap.mean_points, snap.mean_bytes);
        for (int s = 0; s < kSegmentCount; ++s) {
            std::printf(" | %s p50=%.2f p95=%.2f max=%.2f ms", SegmentName(s),
                        snap.segment[s].p50 / 1000.0, snap.segment[s].p95 / 1000.0, snap.segment[s].max / 1000.0);
        }
        std::printf(" | overshoot p50=%.2f p95=%.2f max=%.2f ms", snap.overshoot.p50 / 1000.0,
                    snap.overshoot.p95 / 1000.0, snap.overshoot.max / 1000.0);
        std::printf(" | residual p50=%.2f p95=%.2f max=%.2f ms", snap.residual.p50 / 1000.0,
                    snap.residual.p95 / 1000.0, snap.residual.max / 1000.0);
        std::printf(" | tick p50=%.2f p95=%.2f max=%.2f ms | nominal=%.2f ms | clients=%.1f",
                    snap.tick.p50 / 1000.0, snap.tick.p95 / 1000.0, snap.tick.max / 1000.0,
                    snap.nominal_wait_us / 1000.0, snap.mean_clients);
        // Row #153: how many times the exposure config was pushed in this
        // window — the skip is visible here (1 per window instead of one per
        // tick).
        std::printf(" | exposure n=%u", snap.exposure_applies);
        std::printf(" | bcast_per_client=%.2f us (derived mean; per-client send stats need engine/**)\n",
                    snap.mean_clients > 0 ? (snap.segment[kBroadcast].p50 / snap.mean_clients) : 0.0);
        std::fflush(stdout);

        for (int s = 0; s < kSegmentCount; ++s) {
            counts_[s] = 0;
        }
        period_n_ = points_n_ = bytes_n_ = 0;
        overshoot_n_ = 0;
        residual_n_ = 0;
        clients_n_ = 0;
        exposure_applies_ = 0;
    }

    static Percentiles PercentilesOf(const int64_t* values, int count) {
        Percentiles p;
        p.p50 = Percentile(values, count, 50);
        p.p95 = Percentile(values, count, 95);
        p.max = Percentile(values, count, 100);
        return p;
    }

    static void Push(int64_t* buffer, int& count, int64_t value) {
        if (count < kCapacity) buffer[count++] = value;
    }

    static int64_t Sum(const int64_t* buffer, int count) {
        int64_t total = 0;
        for (int i = 0; i < count; ++i) total += buffer[i];
        return total;
    }

    // Nearest-rank percentile (p in percent); sorted copy on the stack, so the
    // flush path does not allocate.
    static int64_t Percentile(const int64_t* values, int count, int percent) {
        if (count <= 0) return 0;
        int64_t scratch[kCapacity];
        std::copy(values, values + count, scratch);
        std::sort(scratch, scratch + count);
        int index = (percent * count + 99) / 100 - 1;
        if (index < 0) index = 0;
        if (index >= count) index = count - 1;
        return scratch[index];
    }

    int64_t last_us_ = 0;
    int64_t flush_start_us_ = 0;
    bool have_last_ = false;
    bool flush_started_ = false;

    int64_t samples_[kSegmentCount][kCapacity] = {};
    int counts_[kSegmentCount] = {};
    int64_t periods_[kCapacity] = {};
    int period_n_ = 0;
    int64_t points_[kCapacity] = {};
    int points_n_ = 0;
    int64_t bytes_[kCapacity] = {};
    int bytes_n_ = 0;
    int64_t overshoot_us_[kCapacity] = {};
    int overshoot_n_ = 0;
    int64_t residual_us_[kCapacity] = {};
    int residual_n_ = 0;
    int64_t clients_[kCapacity] = {};
    int clients_n_ = 0;
    uint32_t exposure_applies_ = 0;
    long long cumulative_exposure_applies_ = 0;
    int64_t pending_work_us_ = 0;
    int64_t last_nominal_wait_us_ = 0;

    long long ticks_ = 0;
    long long cumulative_points_ = 0;
    long long cumulative_bytes_ = 0;
    int64_t cumulative_us_[kSegmentCount] = {};
    int64_t max_us_[kSegmentCount] = {};
    long long cumulative_overshoot_us_ = 0;
    long long cumulative_residual_us_ = 0;
    int64_t max_overshoot_us_ = 0;
    bool enabled_ = true;
};

// Row #151 addendum: does the cloud *content* change, or only its delivery?
// Hash the payload every tick and report the change rate and the run lengths
// (how many consecutive ticks carry the same hash). A delivery rate of 24 Hz
// with a much lower content-change rate means the source is slower than the
// listener; equal rates mean every delivered frame is new.
class CloudChangeTracker {
public:
    void Observe(uint64_t hash) {
        ++ticks_;
        if (!have_) {
            have_ = true;
            last_hash_ = hash;
            return;
        }
        if (hash == last_hash_) {
            ++current_run_;
            return;
        }
        ++changes_;
        Push(runs_, run_n_, current_run_ + 1);  // ticks in the run that just ended
        current_run_ = 0;
        last_hash_ = hash;
    }

    struct PercentilesOf {
        int64_t p50 = 0;
        int64_t p95 = 0;
        int64_t max = 0;
    };
    struct Snapshot {
        int64_t ticks = 0;
        int64_t changes = 0;
        double change_rate = 0.0;  // changes per second over the window
        PercentilesOf run;         // ticks per content run
    };

    Snapshot TakeSnapshot(double window_s) const {
        Snapshot snap;
        snap.ticks = ticks_;
        snap.changes = changes_;
        snap.change_rate = window_s > 0 ? static_cast<double>(changes_) / window_s : 0.0;
        snap.run.p50 = Percentile(runs_, run_n_, 50);
        snap.run.p95 = Percentile(runs_, run_n_, 95);
        snap.run.max = Percentile(runs_, run_n_, 100);
        return snap;
    }

    void PrintAndReset(const char* tag, double window_s) {
        const Snapshot snap = TakeSnapshot(window_s);
        std::printf("[perf] cloud %s %.1fs clock=steady_clock ticks=%lld changes=%lld change_rate=%.1f/s"
                    " run_ticks p50=%lld p95=%lld max=%lld\n",
                    tag, window_s, static_cast<long long>(snap.ticks), static_cast<long long>(snap.changes),
                    snap.change_rate, static_cast<long long>(snap.run.p50),
                    static_cast<long long>(snap.run.p95), static_cast<long long>(snap.run.max));
        std::fflush(stdout);
        ticks_ = changes_ = run_n_ = 0;
        current_run_ = 0;
        have_ = false;
    }

private:
    static constexpr int kCapacity = 512;
    static void Push(int64_t* buffer, int& count, int64_t value) {
        if (count < kCapacity) buffer[count++] = value;
    }
    static int64_t Percentile(const int64_t* values, int count, int percent) {
        if (count <= 0) return 0;
        int64_t scratch[kCapacity];
        std::copy(values, values + count, scratch);
        std::sort(scratch, scratch + count);
        int index = (percent * count + 99) / 100 - 1;
        if (index < 0) index = 0;
        if (index >= count) index = count - 1;
        return scratch[index];
    }

    int64_t ticks_ = 0;
    int64_t changes_ = 0;
    int64_t current_run_ = 0;
    uint64_t last_hash_ = 0;
    bool have_ = false;
    int64_t runs_[kCapacity] = {};
    int run_n_ = 0;
};

}  // namespace openscanner::perf
