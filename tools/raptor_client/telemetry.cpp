#include "telemetry.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>

namespace raptor {
namespace {

// Nearest-rank: the p-th percentile is an actual sample.
uint64_t Percentile(const std::vector<uint64_t>& sorted, unsigned p) {
    if (sorted.empty()) return 0;
    // ceil(p * n / 100) - 1, in integer arithmetic.
    const uint64_t rank = ((uint64_t)p * sorted.size() + 99) / 100;
    size_t index = rank == 0 ? 0 : (size_t)(rank - 1);
    if (index >= sorted.size()) index = sorted.size() - 1;
    return sorted[index];
}

}  // namespace

void Telemetry::Begin() {
    began_us_ = clock_();
    began_ = true;
}

void Telemetry::RecordFrameCall(uint64_t elapsed_us, size_t request_bytes, size_t reply_bytes, bool timed_out) {
    frame_us_.push_back(elapsed_us);
    if (timed_out) ++timeouts_;
    wire_bytes_ += (uint64_t)request_bytes + (uint64_t)reply_bytes;
}

void Telemetry::RecordOtherCall(size_t request_bytes, size_t reply_bytes) {
    wire_bytes_ += (uint64_t)request_bytes + (uint64_t)reply_bytes;
}

void Telemetry::AddPayload(size_t bytes) { payload_bytes_ += (uint64_t)bytes; }

void Telemetry::MarkFirstFrame() {
    if (have_first_frame_ || !began_) return;
    have_first_frame_ = true;
    const uint64_t now = clock_();
    first_frame_us_ = now > began_us_ ? now - began_us_ : 0;
}

TimingSummary Telemetry::Snapshot() const {
    TimingSummary s;
    s.calls = frame_us_.size();
    s.timeouts = timeouts_;
    s.payload_bytes = payload_bytes_;
    s.wire_bytes = wire_bytes_;
    if (began_) {
        const uint64_t now = clock_();
        s.window_us = now > began_us_ ? now - began_us_ : 0;
    }
    if (!frame_us_.empty()) {
        std::vector<uint64_t> sorted = frame_us_;
        std::sort(sorted.begin(), sorted.end());
        s.p50_us = Percentile(sorted, 50);
        s.p95_us = Percentile(sorted, 95);
        s.max_us = sorted.back();
        uint64_t sum = 0;
        for (uint64_t v : sorted) sum += v;
        s.mean_us = (sum + (uint64_t)sorted.size() / 2) / (uint64_t)sorted.size();
    }
    if (s.window_us > 0) {
        const double seconds = (double)s.window_us / 1e6;
        s.payload_bytes_per_s = (double)s.payload_bytes / seconds;
        s.wire_bytes_per_s = (double)s.wire_bytes / seconds;
    }
    s.have_first_frame = have_first_frame_;
    s.first_frame_us = first_frame_us_;
    return s;
}

std::string FormatTimingSummary(const TimingSummary& s, const std::string& interface) {
    const std::string first_frame =
        s.have_first_frame ? std::to_string(s.first_frame_us) + "us" : std::string("none");
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "[timing] %s: n=%llu clock=%s p50=%lluus p95=%lluus max=%lluus mean=%lluus; "
                  "payload=%.0fB/s wire=%.0fB/s; first_frame=%s; timeouts=%llu; window=%lluus",
                  interface.c_str(), (unsigned long long)s.calls, Telemetry::clock_name(),
                  (unsigned long long)s.p50_us, (unsigned long long)s.p95_us,
                  (unsigned long long)s.max_us, (unsigned long long)s.mean_us, s.payload_bytes_per_s,
                  s.wire_bytes_per_s, first_frame.c_str(), (unsigned long long)s.timeouts,
                  (unsigned long long)s.window_us);
    return buf;
}

}  // namespace raptor
