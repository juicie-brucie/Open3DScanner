// Row #171's device-free test: the capture sink and its bounds, the mock
// source, the capture-file reader, and the bounded/fail-closed laser and
// exposure command path. No device, no vendor SDK, no network.
#include "raw_capture.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace rc = raw_capture;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

fs::path TempFile(const std::string& tag) {
    static int counter = 0;
    const fs::path dir = fs::temp_directory_path() /
                         ("openscanner-raw-capture-" + std::to_string(++counter) + "-" + tag);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir / "capture.rcap";
}

uint32_t Grey() { return 0x59455247u; }   // 'GREY'
uint32_t Z16() { return 0x5A313620u; }    // 'Z16 '

// A source that fails with a named error on the second pull.
class FailingSource : public rc::FrameSource {
public:
    bool Next(rc::Frame* frame, std::string* error) override {
        if (++pulls_ == 1) {
            frame->kind = rc::StreamKind::kIr;
            frame->seq = 0;
            frame->timestamp_us = 1;
            frame->width = 2;
            frame->height = 2;
            frame->stride = 2;
            frame->format = Grey();
            frame->data.assign(4, 0x7f);
            return true;
        }
        *error = "the endpoint stalled";
        return false;
    }
    bool configured() const override { return true; }
    std::string configuration_error() const override { return ""; }

private:
    int pulls_ = 0;
};

// Records every send, and replays a scripted status per call.
class RecordingTransport : public rc::CommandTransport {
public:
    explicit RecordingTransport(bool configured = true) : configured_(configured) {}

    void Script(std::vector<rc::CommandStatus> statuses) { statuses_ = std::move(statuses); }

    rc::CommandStatus Send(uint32_t property, double value) override {
        properties.push_back(property);
        values.push_back(value);
        ++calls;
        if (statuses_.empty()) return rc::CommandStatus::kAccepted;
        const size_t index = calls - 1 < statuses_.size() ? calls - 1 : statuses_.size() - 1;
        return statuses_[index];
    }
    bool configured() const override { return configured_; }
    std::string configuration_error() const override {
        return configured_ ? "" : "not-configured: test transport is off";
    }

    std::vector<uint32_t> properties;
    std::vector<double> values;
    int calls = 0;

private:
    bool configured_ = true;
    std::vector<rc::CommandStatus> statuses_;
};

}  // namespace

int main() {
    // --- the table: known identity/ports, unknown USB side ----------------
    {
        const rc::DescriptorTable& table = rc::Table();
        Check(table.identity_knowledge == rc::Knowledge::kKnown, "identity is known (VID/PID)");
        Check(table.ir.rpc_port == 10013 && table.ir.rpc_port_knowledge == rc::Knowledge::kKnown,
              "IR's logical RPC port is known (10013)");
        Check(table.depth.rpc_port == 10010 && table.depth.rpc_port_knowledge == rc::Knowledge::kKnown,
              "depth's logical RPC port is known (10010)");
        Check(table.ir.endpoint.knowledge == rc::Knowledge::kUnknown, "IR's USB endpoint is unknown");
        Check(table.depth.endpoint.knowledge == rc::Knowledge::kUnknown, "depth's USB endpoint is unknown");
        Check(table.ir.pixel_format == 0 && table.depth.pixel_format == 0,
              "the UVC formats are unknown (not guessed)");
        Check(!rc::StreamConfigured(table.ir) && !rc::StreamConfigured(table.depth),
              "neither stream is configured yet");
        Check(table.rgb_rpc_port_knowledge == rc::Knowledge::kKnown &&
                  table.imu_rpc_port_knowledge == rc::Knowledge::kKnown,
              "the RGB/IMU logical ports are marked known");
        Check(table.control.knowledge == rc::Knowledge::kUnknown, "the HID/vendor control map is unknown");
    }

    // --- the production UVC source refuses, naming what is missing --------
    {
        const fs::path path = TempFile("unconfigured");
        rc::FileSink sink;
        std::string error;
        Check(sink.Open(path.string(), &error), "the sink opens");
        rc::UvcSource source;
        rc::CaptureRunner runner;
        rc::Limits limits;
        const rc::CaptureSummary summary = runner.Run(source, sink, limits);
        sink.Close();
        Check(summary.reason == rc::StopReason::kNotConfigured, "an unconfigured UVC source stops as not-configured");
        Check(summary.frames == 0, "it writes no frames");
        Check(summary.detail.find("not-configured") == 0, "the detail starts with not-configured");
        Check(summary.detail.find("bEndpointAddress") != std::string::npos,
              "the detail names a missing endpoint field");
    }

    // --- a mock IR capture, read back -------------------------------------
    {
        const fs::path path = TempFile("ir");
        rc::FileSink sink;
        std::string error;
        Check(sink.Open(path.string(), &error), "the IR sink opens");
        rc::MockSource source(rc::StreamKind::kIr, 32, 24, Grey(), 0, 5);
        rc::CaptureRunner runner;
        rc::Limits limits;
        limits.max_frames = 5;
        const rc::CaptureSummary summary = runner.Run(source, sink, limits);
        sink.Close();
        Check(summary.reason == rc::StopReason::kFrames, "the frame bound stops the run");
        Check(summary.frames == 5 && summary.per_stream[0] == 5, "five IR frames written");
        // The summary counts records; the file additionally carries its 16-byte header.
        Check(summary.bytes + 16 == sink.bytes_written(), "the summary's byte count matches the file");

        std::vector<rc::CapturedRecord> records;
        Check(rc::ReadCaptureFile(path.string(), &records, &error), "the capture reads back");
        Check(records.size() == 5, "five records read back");
        bool fields_ok = !records.empty();
        for (const auto& record : records) {
            fields_ok = fields_ok && record.frame.kind == rc::StreamKind::kIr &&
                        record.frame.width == 32 && record.frame.height == 24 &&
                        record.frame.stride == 32 && record.frame.format == Grey() &&
                        record.frame.data.size() == 32 * 24;
        }
        Check(fields_ok, "each record carries its kind, size, format and stride");
        Check(records[0].frame.seq == 0 && records[4].frame.seq == 4, "the sequence is recorded");
    }

    // --- both streams into one sink ---------------------------------------
    {
        const fs::path path = TempFile("both");
        rc::FileSink sink;
        std::string error;
        Check(sink.Open(path.string(), &error), "the shared sink opens");
        rc::CaptureRunner runner;
        rc::Limits limits;
        limits.max_frames = 3;
        rc::MockSource ir(rc::StreamKind::kIr, 8, 4, Grey(), 0, 3);
        const rc::CaptureSummary first = runner.Run(ir, sink, limits);
        rc::MockSource depth(rc::StreamKind::kDepth, 8, 4, Z16(), 0, 3);
        const rc::CaptureSummary second = runner.Run(depth, sink, limits);
        sink.Close();
        Check(first.frames == 3 && second.frames == 3, "both streams captured");
        std::vector<rc::CapturedRecord> records;
        Check(rc::ReadCaptureFile(path.string(), &records, &error), "the two-stream capture reads back");
        uint64_t ir_count = 0, depth_count = 0;
        for (const auto& record : records) {
            if (record.frame.kind == rc::StreamKind::kIr) ++ir_count;
            if (record.frame.kind == rc::StreamKind::kDepth) {
                ++depth_count;
                Check(record.frame.stride == 16 && record.frame.data.size() == 16 * 4,
                      "a depth record keeps its 2-byte stride and size");
            }
        }
        Check(ir_count == 3 && depth_count == 3, "the reader separates the streams");
    }

    // --- the byte bound: a partial record is never written ----------------
    {
        const fs::path path = TempFile("bytes");
        rc::FileSink sink;
        std::string error;
        Check(sink.Open(path.string(), &error), "the byte-bound sink opens");
        rc::MockSource source(rc::StreamKind::kIr, 16, 4, Grey(), 0, 10);
        rc::CaptureRunner runner;
        rc::Limits limits;
        limits.max_bytes = 16;  // the file header only
        const rc::CaptureSummary summary = runner.Run(source, sink, limits);
        sink.Close();
        Check(summary.reason == rc::StopReason::kBytes, "the byte bound stops the run");
        Check(summary.frames == 0, "no partial record is written");
        std::vector<rc::CapturedRecord> records;
        Check(rc::ReadCaptureFile(path.string(), &records, &error), "a header-only capture reads back");
        Check(records.empty(), "it holds no records");
    }
    {
        const fs::path path = TempFile("bytes-exact");
        rc::FileSink sink;
        std::string error;
        Check(sink.Open(path.string(), &error), "the exact-budget sink opens");
        rc::MockSource source(rc::StreamKind::kIr, 16, 4, Grey(), 0, 10);
        rc::CaptureRunner runner;
        rc::Limits limits;
        // File header + exactly one record (40-byte record header + 64 bytes).
        limits.max_bytes = 16 + 40 + 64;
        const rc::CaptureSummary summary = runner.Run(source, sink, limits);
        sink.Close();
        Check(summary.frames == 1, "a frame that fits exactly is written");
        Check(summary.reason == rc::StopReason::kBytes, "the next frame is refused at the bound");
    }

    // --- the time bound ----------------------------------------------------
    {
        const fs::path path = TempFile("time");
        rc::FileSink sink;
        std::string error;
        Check(sink.Open(path.string(), &error), "the time-bound sink opens");
        rc::MockSource source(rc::StreamKind::kIr, 4, 4, Grey(), 20, 100);
        rc::CaptureRunner runner;
        rc::Limits limits;
        limits.max_duration_ms = 1;
        const rc::CaptureSummary summary = runner.Run(source, sink, limits);
        sink.Close();
        Check(summary.reason == rc::StopReason::kDuration, "the time bound stops the run");
        Check(summary.frames <= 1, "it stops within one frame of the budget");
    }

    // --- a source failure is named, not swallowed -------------------------
    {
        const fs::path path = TempFile("source-error");
        rc::FileSink sink;
        std::string error;
        Check(sink.Open(path.string(), &error), "the failing-source sink opens");
        FailingSource source;
        rc::CaptureRunner runner;
        rc::Limits limits;
        const rc::CaptureSummary summary = runner.Run(source, sink, limits);
        sink.Close();
        Check(summary.reason == rc::StopReason::kSourceError, "a source error stops the run");
        Check(summary.detail == "the endpoint stalled", "the source's reason is carried");
        Check(!summary.ok(), "a failed capture is not ok()");
    }

    // --- the sink refuses a frame with no format/stride -------------------
    {
        const fs::path path = TempFile("bad-frame");
        rc::FileSink sink;
        std::string error;
        Check(sink.Open(path.string(), &error), "the sink opens for a bad frame");
        rc::Frame frame;
        frame.kind = rc::StreamKind::kIr;
        frame.data.assign(4, 0);
        Check(!sink.Write(frame, &error), "a frame with no format/stride is refused");
        sink.Close();
    }

    // --- a truncated capture is a named read error ------------------------
    {
        const fs::path path = TempFile("truncated");
        {
            rc::FileSink sink;
            std::string error;
            sink.Open(path.string(), &error);
            rc::MockSource source(rc::StreamKind::kIr, 8, 8, Grey(), 0, 2);
            rc::CaptureRunner runner;
            rc::Limits limits;
            runner.Run(source, sink, limits);
            sink.Close();
        }
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        fs::resize_file(path, size - 10, ec);
        std::vector<rc::CapturedRecord> records;
        std::string error;
        Check(!rc::ReadCaptureFile(path.string(), &records, &error), "a truncated capture fails to read");
        Check(!error.empty(), "the read error is named");
    }

    // --- the command path: #101's sequences -------------------------------
    {
        RecordingTransport transport;
        rc::VendorCommandPath path(&transport);
        const rc::CommandResult on = path.SetLaser(true);
        Check(on.ok(), "laser on is accepted");
        Check((transport.properties == std::vector<uint32_t>{3, 152, 153}),
              "laser on sends the strict 3,152,153 sequence");
        Check(transport.values.size() == 3 && transport.values[0] == 1 && transport.values[2] == 1,
              "every laser property carries 1");

        transport.properties.clear();
        transport.values.clear();
        const rc::CommandResult off = path.SetLaser(false);
        Check(off.ok() && transport.values.size() == 3 && transport.values[1] == 0, "laser off carries 0");

        transport.properties.clear();
        transport.values.clear();
        const rc::CommandResult exposure = path.SetExposure(500000.0);
        Check(exposure.ok(), "exposure is accepted");
        Check((transport.properties == std::vector<uint32_t>{154, 155}),
              "exposure sends 154 and 155 together");
        Check(transport.values.size() == 2 && transport.values[0] == 500000.0 && transport.values[1] == 500000.0,
              "both exposure properties carry the same value");
    }

    // --- fail-closed: a rejection stops the sequence ----------------------
    {
        RecordingTransport transport;
        transport.Script({rc::CommandStatus::kAccepted, rc::CommandStatus::kRejected});
        rc::VendorCommandPath path(&transport);
        const rc::CommandResult result = path.SetLaser(true);
        Check(result.status == rc::CommandStatus::kRejected, "a rejection is reported");
        Check(result.property == 152, "the failing property is named");
        Check(transport.calls == 2, "nothing after the rejection is sent");
    }

    // --- bounded retries on a timeout -------------------------------------
    {
        RecordingTransport transport;
        transport.Script({rc::CommandStatus::kTimeout});
        rc::VendorCommandPath path(&transport, 2);
        const rc::CommandResult result = path.SetLaser(true);
        Check(result.status == rc::CommandStatus::kTimeout, "a timeout is reported");
        Check(result.attempt == 2 && transport.calls == 2, "the retry budget bounds the attempts");
        Check(result.property == 3, "the timeout names the first property");
    }

    // --- invalid values never reach the transport -------------------------
    {
        RecordingTransport transport;
        rc::VendorCommandPath path(&transport);
        Check(path.SetExposure(0.0).status == rc::CommandStatus::kRejected, "zero exposure is refused");
        Check(path.SetExposure(-1.0).status == rc::CommandStatus::kRejected, "negative exposure is refused");
        Check(path.SetExposure(std::numeric_limits<double>::quiet_NaN()).status == rc::CommandStatus::kRejected,
              "NaN exposure is refused");
        Check(transport.calls == 0, "no invalid value reached the transport");
    }

    // --- an unconfigured transport refuses without sending ----------------
    {
        RecordingTransport transport(false);
        rc::VendorCommandPath path(&transport);
        const rc::CommandResult result = path.SetLaser(true);
        Check(result.status == rc::CommandStatus::kNotConfigured, "an unconfigured transport is not-configured");
        Check(transport.calls == 0, "nothing is sent");

        rc::UnconfiguredHidTransport hid;
        Check(!hid.configured(), "the production HID transport is unconfigured");
        Check(hid.Send(3, 1.0) == rc::CommandStatus::kNotConfigured, "it refuses every property");
        rc::VendorCommandPath hid_path(&hid);
        const rc::CommandResult hid_result = hid_path.SetExposure(500000.0);
        Check(hid_result.status == rc::CommandStatus::kNotConfigured, "the command path reports it");
        Check(hid_result.detail.find("not-configured") == 0, "the refusal is named");
    }

    std::printf("[raw-capture] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
