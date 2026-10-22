// Row #171's raw capture path. See raw_capture.h for the shape and the two
// seams (FrameSource, CommandTransport) the descriptor capture will fill.

#include "raw_capture.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace raw_capture {
namespace {

constexpr char kMagic[4] = {'R', 'C', 'A', 'P'};
constexpr uint32_t kVersion = 1;
constexpr size_t kFileHeaderBytes = 16;
constexpr size_t kRecordHeaderBytes = 40;

void PutU32(std::string* out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out->push_back(static_cast<char>((value >> (i * 8)) & 0xff));
}
void PutU64(std::string* out, uint64_t value) {
    for (int i = 0; i < 8; ++i) out->push_back(static_cast<char>((value >> (i * 8)) & 0xff));
}
bool GetU32(std::istream& in, uint32_t* value) {
    uint8_t bytes[4];
    in.read(reinterpret_cast<char*>(bytes), 4);
    if (in.gcount() != 4) return false;
    *value = 0;
    for (int i = 0; i < 4; ++i) *value |= static_cast<uint32_t>(bytes[i]) << (i * 8);
    return true;
}
bool GetU64(std::istream& in, uint64_t* value) {
    uint8_t bytes[8];
    in.read(reinterpret_cast<char*>(bytes), 8);
    if (in.gcount() != 8) return false;
    *value = 0;
    for (int i = 0; i < 8; ++i) *value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    return true;
}

uint32_t Fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

uint64_t NowUs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

std::string MissingFields(const StreamSpec& spec) {
    std::string missing;
    auto add = [&missing](const char* field) {
        if (!missing.empty()) missing += ", ";
        missing += field;
    };
    if (spec.endpoint.knowledge != Knowledge::kKnown) add("endpoint (interface/address/transfer)");
    if (spec.endpoint.interface_number == kUnset) add("bInterfaceNumber");
    if (spec.endpoint.endpoint_address == kUnset) add("bEndpointAddress");
    if (spec.endpoint.transfer == TransferType::kUnknown) add("transfer type");
    if (spec.pixel_format == 0) add("UVC pixel format");
    if (spec.bytes_per_line == 0) add("stride (bytes per line)");
    if (spec.width == 0 || spec.height == 0) add("width/height");
    return missing;
}

}  // namespace

const char* StopReasonName(StopReason reason) {
    switch (reason) {
        case StopReason::kFrames: return "limit-frames";
        case StopReason::kBytes: return "limit-bytes";
        case StopReason::kDuration: return "limit-time";
        case StopReason::kSourceEnded: return "source-ended";
        case StopReason::kSourceError: return "source-error";
        case StopReason::kNotConfigured: return "not-configured";
        case StopReason::kSinkError: return "sink-error";
    }
    return "unknown";
}

// --- the sink --------------------------------------------------------------

FileSink::~FileSink() { Close(); }

bool FileSink::Open(const std::string& path, std::string* error) {
    Close();
    path_ = path;
    out_.open(path, std::ios::binary | std::ios::trunc);
    if (!out_) {
        *error = "cannot open the sink for writing: " + path;
        return false;
    }
    std::string header(kMagic, 4);
    PutU32(&header, kVersion);
    PutU32(&header, 0);
    PutU32(&header, 0);
    out_.write(header.data(), static_cast<std::streamsize>(header.size()));
    if (!out_) {
        *error = "cannot write the sink header: " + path;
        return false;
    }
    bytes_written_ = kFileHeaderBytes;
    open_ = true;
    return true;
}

bool FileSink::Write(const Frame& frame, std::string* error) {
    if (!open_) {
        *error = "the sink is not open";
        return false;
    }
    if (frame.stride == 0 || frame.width == 0 || frame.height == 0 || frame.format == 0 ||
        frame.data.empty()) {
        *error = "the frame has no format/stride/size to record";
        return false;
    }
    std::string header;
    header.reserve(kRecordHeaderBytes);
    PutU32(&header, static_cast<uint32_t>(frame.kind));
    PutU32(&header, frame.format);
    PutU32(&header, frame.width);
    PutU32(&header, frame.height);
    PutU32(&header, frame.stride);
    PutU32(&header, static_cast<uint32_t>(frame.data.size()));
    PutU64(&header, frame.seq);
    PutU64(&header, frame.timestamp_us);
    out_.write(header.data(), static_cast<std::streamsize>(header.size()));
    out_.write(reinterpret_cast<const char*>(frame.data.data()),
               static_cast<std::streamsize>(frame.data.size()));
    if (!out_) {
        *error = "cannot write a frame record to the sink";
        return false;
    }
    bytes_written_ += header.size() + frame.data.size();
    return true;
}

bool FileSink::Close() {
    if (open_) {
        out_.flush();
        out_.close();
        open_ = false;
    }
    return true;
}

bool ReadCaptureFile(const std::string& path, std::vector<CapturedRecord>* records, std::string* error) {
    records->clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *error = "cannot open the capture for reading: " + path;
        return false;
    }
    char magic[4];
    in.read(magic, 4);
    if (in.gcount() != 4 || std::memcmp(magic, kMagic, 4) != 0) {
        *error = "not a raw-capture file (magic)";
        return false;
    }
    uint32_t version = 0;
    uint32_t reserved = 0;
    if (!GetU32(in, &version) || !GetU32(in, &reserved) || !GetU32(in, &reserved)) {
        *error = "short capture header";
        return false;
    }
    if (version != kVersion) {
        *error = "unsupported capture version " + std::to_string(version);
        return false;
    }
    while (true) {
        uint32_t kind = 0, format = 0, width = 0, height = 0, stride = 0, frame_bytes = 0;
        uint64_t seq = 0, timestamp_us = 0;
        if (!GetU32(in, &kind)) break;  // end of file
        if (!GetU32(in, &format) || !GetU32(in, &width) || !GetU32(in, &height) ||
            !GetU32(in, &stride) || !GetU32(in, &frame_bytes) || !GetU64(in, &seq) ||
            !GetU64(in, &timestamp_us)) {
            *error = "truncated record header";
            return false;
        }
        if (kind >= static_cast<uint32_t>(kStreamCount)) {
            *error = "unknown stream kind " + std::to_string(kind);
            return false;
        }
        CapturedRecord record;
        record.frame.kind = static_cast<StreamKind>(kind);
        record.frame.format = format;
        record.frame.width = width;
        record.frame.height = height;
        record.frame.stride = stride;
        record.frame.seq = seq;
        record.frame.timestamp_us = timestamp_us;
        record.frame.data.resize(frame_bytes);
        if (frame_bytes > 0) {
            in.read(reinterpret_cast<char*>(record.frame.data.data()),
                    static_cast<std::streamsize>(frame_bytes));
            if (static_cast<uint32_t>(in.gcount()) != frame_bytes) {
                *error = "truncated frame payload";
                return false;
            }
        }
        records->push_back(std::move(record));
    }
    return true;
}

// --- the loop --------------------------------------------------------------

CaptureSummary CaptureRunner::Run(FrameSource& source, FileSink& sink, const Limits& limits) {
    CaptureSummary summary;
    const uint64_t start_us = NowUs();
    auto elapsed_ms = [&start_us]() { return (NowUs() - start_us) / 1000; };

    if (!source.configured()) {
        summary.reason = StopReason::kNotConfigured;
        summary.detail = source.configuration_error();
        return summary;
    }

    while (true) {
        if (summary.frames >= limits.max_frames) {
            summary.reason = StopReason::kFrames;
            break;
        }
        if (elapsed_ms() >= limits.max_duration_ms) {
            summary.reason = StopReason::kDuration;
            break;
        }
        Frame frame;
        std::string error;
        if (!source.Next(&frame, &error)) {
            if (error.empty()) {
                summary.reason = StopReason::kSourceEnded;
            } else {
                summary.reason = StopReason::kSourceError;
                summary.detail = error;
            }
            break;
        }
        const uint64_t record_bytes = kRecordHeaderBytes + frame.data.size();
        if (summary.bytes + record_bytes > limits.max_bytes) {
            // Never write a partial record: stop with the frame unwritten.
            summary.reason = StopReason::kBytes;
            summary.detail = "the next frame (" + std::to_string(frame.data.size()) +
                             " bytes) would exceed the byte budget";
            break;
        }
        if (!sink.Write(frame, &error)) {
            summary.reason = StopReason::kSinkError;
            summary.detail = error;
            break;
        }
        ++summary.frames;
        summary.bytes += record_bytes;
        summary.per_stream[static_cast<int>(frame.kind)]++;
    }
    summary.duration_ms = elapsed_ms();
    return summary;
}

// --- the device-free source ------------------------------------------------

MockSource::MockSource(StreamKind kind, uint32_t width, uint32_t height, uint32_t format, int gap_ms,
                       uint64_t frame_limit)
    : kind_(kind),
      width_(width),
      height_(height),
      format_(format),
      gap_ms_(gap_ms),
      frame_limit_(frame_limit) {}

bool MockSource::Next(Frame* frame, std::string* error) {
    (void)error;
    if (emitted_ >= frame_limit_) return false;
    if (gap_ms_ > 0) std::this_thread::sleep_for(std::chrono::milliseconds(gap_ms_));
    const bool depth = kind_ == StreamKind::kDepth;
    const bool rgb = kind_ == StreamKind::kRgb;
    frame->kind = kind_;
    frame->seq = emitted_;
    frame->timestamp_us = NowUs();
    frame->width = width_;
    frame->height = height_;
    frame->stride = width_ * (depth || rgb ? 2u : 1u);
    frame->format = format_;
    frame->data.resize(static_cast<size_t>(frame->stride) * height_);
    for (uint32_t y = 0; y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            const size_t at = static_cast<size_t>(y) * frame->stride + x * (depth || rgb ? 2u : 1u);
            if (depth) {
                // A ramp that changes with the frame, so a still image is visible in a diff.
                const uint16_t value = static_cast<uint16_t>(((x + y + emitted_ * 7) * 64) & 0xffff);
                frame->data[at] = static_cast<uint8_t>(value & 0xff);
                frame->data[at + 1] = static_cast<uint8_t>(value >> 8);
            } else if (rgb) {
                frame->data[at] = static_cast<uint8_t>((x + emitted_) & 0xff);
                frame->data[at + 1] = static_cast<uint8_t>((y + emitted_) & 0xff);
            } else {
                frame->data[at] = static_cast<uint8_t>(((x / 8 + y / 8 + emitted_) % 2) ? 0xff : 0x00);
            }
        }
    }
    ++emitted_;
    return true;
}

// --- the command path ------------------------------------------------------

const char* CommandStatusName(CommandStatus status) {
    switch (status) {
        case CommandStatus::kAccepted: return "accepted";
        case CommandStatus::kRejected: return "rejected";
        case CommandStatus::kTimeout: return "timeout";
        case CommandStatus::kNotConfigured: return "not-configured";
        case CommandStatus::kTransportError: return "transport-error";
    }
    return "unknown";
}

const PropertySpec* LookupProperty(uint32_t id) {
    // #101 §4: the observed properties. Names are descriptions, not vendor
    // labels; `evidence` carries the finding and the observed domain.
    static const PropertySpec kProperties[] = {
        {3, "laser on/off", PropertyValueType::kBool, "property-semantics-row101.md §4.1: 0/1, HIGH"},
        {152, "laser group", PropertyValueType::kBool, "property-semantics-row101.md §4.1: 0/1, MEDIUM"},
        {153, "laser mode", PropertyValueType::kBool, "property-semantics-row101.md §4.1: 0/1, MEDIUM"},
        {154, "exposure", PropertyValueType::kFloat,
         "property-semantics-row101.md §4.2: float 500k-1.5M observed, MEDIUM"},
        {155, "laser current", PropertyValueType::kFloat,
         "property-semantics-row101.md §4.2: float, same value as 154, MEDIUM"},
    };
    for (const PropertySpec& spec : kProperties) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

CommandResult VendorCommandPath::SendSequence(const uint32_t* properties, size_t count, double value) {
    CommandResult result;
    if (transport_ == nullptr) {
        result.status = CommandStatus::kNotConfigured;
        result.detail = "no command transport";
        return result;
    }
    if (!transport_->configured()) {
        result.status = CommandStatus::kNotConfigured;
        result.detail = transport_->configuration_error();
        return result;
    }
    for (size_t i = 0; i < count; ++i) {
        const uint32_t property = properties[i];
        const PropertySpec* spec = LookupProperty(property);
        if (spec == nullptr) {
            result.status = CommandStatus::kRejected;
            result.property = property;
            result.detail = "unknown property";
            return result;
        }
        if (spec->type == PropertyValueType::kBool && value != 0.0 && value != 1.0) {
            result.status = CommandStatus::kRejected;
            result.property = property;
            result.detail = "a binary property accepts only 0 or 1";
            return result;
        }
        if (spec->type == PropertyValueType::kFloat && (!std::isfinite(value) || value <= 0.0)) {
            result.status = CommandStatus::kRejected;
            result.property = property;
            result.detail = "a float property must be finite and positive";
            return result;
        }
        for (int attempt = 1; attempt <= max_attempts_; ++attempt) {
            const CommandStatus status = transport_->Send(property, value);
            result.property = property;
            result.attempt = attempt;
            if (status == CommandStatus::kAccepted) break;
            if (status == CommandStatus::kTimeout || status == CommandStatus::kTransportError) {
                if (attempt == max_attempts_) {
                    result.status = status;
                    result.detail = std::string("property ") + std::to_string(property) +
                                    " failed after " + std::to_string(attempt) + " attempt(s)";
                    return result;
                }
                continue;
            }
            // Rejected / not-configured: fail closed, do not retry, do not
            // send the rest of the sequence.
            result.status = status;
            result.detail = std::string("property ") + std::to_string(property) + " was not accepted";
            return result;
        }
    }
    result.status = CommandStatus::kAccepted;
    result.detail = "accepted";
    return result;
}

CommandResult VendorCommandPath::SetLaser(bool on) {
    static const uint32_t kSequence[] = {3, 152, 153};  // #101 §3.1, always in this order
    return SendSequence(kSequence, sizeof(kSequence) / sizeof(kSequence[0]), on ? 1.0 : 0.0);
}

CommandResult VendorCommandPath::SetExposure(double value) {
    static const uint32_t kSequence[] = {154, 155};  // #101 §3.2, always together, same value
    return SendSequence(kSequence, sizeof(kSequence) / sizeof(kSequence[0]), value);
}

}  // namespace raw_capture
