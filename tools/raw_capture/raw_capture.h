// Row #171 (Phase D seed 3): the raw capture path and the bounded vendor
// command path, with the transport seams a device backend fills in.
//
// What is real here: the frame records and their format/stride, the local
// file sink, the read bounds (frames/bytes/time), the capture loop, and the
// laser/exposure command sequences from #101's property map. What is not yet
// real: the UVC endpoint reader and the HID/vendor control transfer — the
// USB descriptor map that would configure them is pending
// (docs/recon/findings/usb-descriptor-map.md). Those two backends live behind
// FrameSource / CommandTransport and refuse with "not-configured" until the
// operator's capture fills descriptor_table.h. They are never guessed.
//
// No vendor SDK, no device, no network: the test drives MockSource and a
// recording transport.
#pragma once

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "descriptor_table.h"

namespace raw_capture {

// --- the capture side ------------------------------------------------------

struct Frame {
    StreamKind kind = StreamKind::kIr;
    uint64_t seq = 0;
    uint64_t timestamp_us = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;        // bytes per row, as the stream delivers it
    uint32_t format = 0;        // UVC format GUID / fourcc
    std::vector<uint8_t> data;
};

struct Limits {
    uint64_t max_frames = 600;          // both streams together
    uint64_t max_bytes = 512ull * 1024 * 1024;  // record bytes; shared across streams into one sink
    uint64_t max_duration_ms = 120000;  // checked between frames: a blocking read can overrun it by one frame
};

enum class StopReason {
    kFrames,
    kBytes,
    kDuration,
    kSourceEnded,
    kSourceError,
    kNotConfigured,
    kSinkError,
};

const char* StopReasonName(StopReason reason);

struct CaptureSummary {
    StopReason reason = StopReason::kSourceEnded;
    uint64_t frames = 0;
    uint64_t bytes = 0;                 // record bytes written (the 16-byte file header is not counted)
    uint64_t per_stream[kStreamCount] = {};
    uint64_t duration_ms = 0;
    std::string detail;                 // names the failure, when there is one
    bool ok() const { return reason != StopReason::kSourceError && reason != StopReason::kSinkError &&
                             reason != StopReason::kNotConfigured; }
};

// The seam the real UVC reader plugs into (IOKit/AVFoundation, once the
// descriptor table has the interface/endpoint/format).
class FrameSource {
public:
    virtual ~FrameSource() = default;
    // Fills *frame. Returns false at the end, on error, or when unconfigured;
    // `error` names the reason and is empty at a clean end.
    virtual bool Next(Frame* frame, std::string* error) = 0;
    virtual bool configured() const = 0;
    virtual std::string configuration_error() const = 0;
};

// The local sink: a self-describing capture file.
//
//   file header (16 bytes, little-endian)
//     magic "RCAP", version u32 = 1, reserved u32 = 0, reserved u32 = 0
//   records, repeated
//     kind u32 (0 IR, 1 depth), format u32, width u32, height u32,
//     stride u32, frame_bytes u32, seq u64, timestamp_us u64, payload
class FileSink {
public:
    ~FileSink();
    bool Open(const std::string& path, std::string* error);
    bool Write(const Frame& frame, std::string* error);
    bool Close();
    uint64_t bytes_written() const { return bytes_written_; }
    const std::string& path() const { return path_; }

private:
    std::ofstream out_;
    std::string path_;
    uint64_t bytes_written_ = 0;
    bool open_ = false;
};

struct CapturedRecord {
    Frame frame;  // metadata plus payload, as read back
};

// Reads a capture file back (the test's and the operator's other half).
bool ReadCaptureFile(const std::string& path, std::vector<CapturedRecord>* records, std::string* error);

// The capture loop: bounded by frames, bytes and wall time, stopping before a
// frame that would exceed the byte bound is written (a partial record is
// never written).
class CaptureRunner {
public:
    CaptureSummary Run(FrameSource& source, FileSink& sink, const Limits& limits);
};

// A deterministic device-free source: classic checkerboard IR and a
// ramped depth image, at a fixed format/stride, `gap_ms` apart.
class MockSource : public FrameSource {
public:
    MockSource(StreamKind kind, uint32_t width, uint32_t height, uint32_t format, int gap_ms,
               uint64_t frame_limit);

    bool Next(Frame* frame, std::string* error) override;
    bool configured() const override { return true; }
    std::string configuration_error() const override { return ""; }

private:
    StreamKind kind_;
    uint32_t width_;
    uint32_t height_;
    uint32_t format_;
    int gap_ms_;
    uint64_t frame_limit_;
    uint64_t emitted_ = 0;
};

// --- the command side ------------------------------------------------------

// #101's property map (docs/recon/findings/property-semantics-row101.md §4).
enum class PropertyValueType { kBool, kFloat };

struct PropertySpec {
    uint32_t id = 0;
    const char* name = "";
    PropertyValueType type = PropertyValueType::kBool;
    const char* evidence = "";     // the finding and the observed domain
};

const PropertySpec* LookupProperty(uint32_t id);

enum class CommandStatus {
    kAccepted,
    kRejected,
    kTimeout,
    kNotConfigured,
    kTransportError,
};

const char* CommandStatusName(CommandStatus status);

struct CommandResult {
    CommandStatus status = CommandStatus::kAccepted;
    uint32_t property = 0;
    int attempt = 0;
    std::string detail;

    bool ok() const { return status == CommandStatus::kAccepted; }
};

// The seam the HID feature report / vendor control transfer plugs into.
class CommandTransport {
public:
    virtual ~CommandTransport() = default;
    virtual CommandStatus Send(uint32_t property, double value) = 0;
    virtual bool configured() const = 0;
    virtual std::string configuration_error() const = 0;
};

// Bounded and fail-closed: each property is tried at most `max_attempts`
// times, a timeout/transport error is retried, a rejection or an
// unconfigured transport is not, and the sequence stops at the first
// failure. The property order is #101's.
class VendorCommandPath {
public:
    explicit VendorCommandPath(CommandTransport* transport, int max_attempts = 2)
        : transport_(transport), max_attempts_(max_attempts < 1 ? 1 : max_attempts) {}

    // Props 3, 152, 153 — the observed strict sequence, same bool value.
    CommandResult SetLaser(bool on);

    // Props 154 and 155 — always set together, same float value.
    CommandResult SetExposure(double value);

private:
    CommandResult SendSequence(const uint32_t* properties, size_t count, double value);

    CommandTransport* transport_ = nullptr;
    int max_attempts_ = 2;
};

// The production transport until the HID/vendor control map is captured: it
// refuses every command instead of writing a guessed report.
class UnconfiguredHidTransport : public CommandTransport {
public:
    CommandStatus Send(uint32_t, double) override { return CommandStatus::kNotConfigured; }
    bool configured() const override { return false; }
    std::string configuration_error() const override {
        return std::string("not-configured: ") + Table().control.missing;
    }
};

// A source that always refuses, for a consumer that has not wired the live
// backend yet (the raptor_shim's transport, row #179). The live source is
// MakeLiveUvcSource(); this one cannot produce frames and says so, so a
// consumer that forgets to wire it fails closed rather than looking live.
class UvcSource : public FrameSource {
public:
    bool Next(Frame*, std::string* error) override {
        *error = configuration_error();
        return false;
    }
    bool configured() const override { return false; }
    std::string configuration_error() const override {
        return "not-configured: this refusing source has no live backend; wire MakeLiveUvcSource "
               "(raw_capture --capture) with descriptor_table.h configured (its facts come from "
               "docs/recon/findings/usb-descriptor-map.md and the probe record)";
    }
};

// The live backends (row #178). null + `why` when the table still lacks a
// required field; the caller reports that verbatim. On a non-macOS build the
// answer is a named "macOS-only" refusal.
std::unique_ptr<FrameSource> MakeLiveUvcSource(StreamKind kind, std::string* why);
std::unique_ptr<CommandTransport> MakeLiveHidTransport(std::string* why);

}  // namespace raw_capture
