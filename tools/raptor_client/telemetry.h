#pragma once
// Frame-path telemetry for raptor_client (row #175).
//
// The roadmap's P1 telemetry table lists what the frame path owed beyond the
// #140 counters: per-call latency, bytes/s, and time-to-first-frame. The
// data/empty share and the 11-element parse are #140's; this is the time half.
//
// Clock: std::chrono::steady_clock (monotonic, never adjusted), microseconds.
// The injected Clock is the deterministic seam for the device-free test: it
// lets a test drive Begin()/MarkFirstFrame()/Snapshot() without waiting.
//
// Units and the rules the summary is computed by:
//   - latency       microseconds, one sample per get_frame call, measured
//                   from the write of the request to the complete reply (or
//                   to the deadline that ended the wait).
//   - percentiles   nearest-rank on the sorted samples:
//                   index = ceil(p * n / 100) - 1, clamped to [0, n-1].
//                   p50 and p95 are therefore actual samples, never
//                   interpolated values.
//   - bytes/s       payload = the primary frame buffer's bytes; wire = the
//                   request frame plus the reply as it arrived. Both over the
//                   window from Begin() to Snapshot(), so idle time in the
//                   run counts, as a card should see it.
//   - first frame   microseconds from Begin() to the first reply that carried
//                   a non-empty payload; set once, later frames do not move it.
//
// Nothing here opens a socket or a device.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace raptor {

// Monotonic microseconds. The production clock, named in every summary.
inline uint64_t SteadyNowUs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

struct TimingSummary {
    uint64_t calls = 0;     // get_frame calls in the latency set
    uint64_t timeouts = 0;  // of those, the ones that hit their deadline
    uint64_t p50_us = 0;
    uint64_t p95_us = 0;
    uint64_t max_us = 0;
    uint64_t mean_us = 0;
    uint64_t payload_bytes = 0;
    uint64_t wire_bytes = 0;
    uint64_t window_us = 0;
    double payload_bytes_per_s = 0.0;
    double wire_bytes_per_s = 0.0;
    bool have_first_frame = false;
    uint64_t first_frame_us = 0;

    bool empty() const { return calls == 0 && wire_bytes == 0; }
};

class Telemetry {
public:
    using Clock = std::function<uint64_t()>;

    explicit Telemetry(Clock clock = &SteadyNowUs) : clock_(std::move(clock)) {}

    // Opens the measurement window. A run calls this once, after the stream
    // is started, so the window covers the frame loop and not the handshake.
    void Begin();

    // One get_frame call: its send -> complete-reply latency and the bytes
    // that crossed the socket for it.
    void RecordFrameCall(uint64_t elapsed_us, size_t request_bytes, size_t reply_bytes, bool timed_out);

    // Any other call on the same connection (the property sets): wire bytes
    // only, so the throughput figure is the connection's, not the frame's.
    void RecordOtherCall(size_t request_bytes, size_t reply_bytes);

    // The primary buffer of a frame reply. Empty replies add zero.
    void AddPayload(size_t bytes);

    // Stamps the first non-empty payload, once.
    void MarkFirstFrame();

    TimingSummary Snapshot() const;

    static const char* clock_name() { return "steady_clock"; }

private:
    Clock clock_;
    uint64_t began_us_ = 0;
    bool began_ = false;
    std::vector<uint64_t> frame_us_;
    uint64_t timeouts_ = 0;
    uint64_t payload_bytes_ = 0;
    uint64_t wire_bytes_ = 0;
    bool have_first_frame_ = false;
    uint64_t first_frame_us_ = 0;
};

// The run-log line. `interface` names the port's interface, so a two-run
// comparison says which card is which.
std::string FormatTimingSummary(const TimingSummary& summary, const std::string& interface);

}  // namespace raptor
