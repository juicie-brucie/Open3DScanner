// The correlation logic (row #182). See correlation.h.

#include "correlation.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>

namespace raw_capture {
namespace {

constexpr int kClassVideo = 0x0E;
constexpr int kSubclassVideoStreaming = 0x02;
constexpr int kClassHid = 0x03;

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

StreamCandidate ClassifyName(const std::string& name, std::string* evidence) {
    const std::string lower = Lower(name);
    if (lower.empty()) {
        *evidence = "no interface name";
        return StreamCandidate::kUnknown;
    }
    // The device names its interfaces per camera ("… IR Camera", "… Depth
    // Camera", "… RGB Camera", "… IMU"). Match the function token; anything
    // else stays unassigned rather than being forced into a stream.
    if (lower.find("infrared") != std::string::npos || lower.find(" ir ") != std::string::npos ||
        lower.find("ir camera") != std::string::npos) {
        *evidence = "name contains an IR token";
        return StreamCandidate::kIr;
    }
    if (lower.find("depth") != std::string::npos) {
        *evidence = "name contains \"depth\"";
        return StreamCandidate::kDepth;
    }
    if (lower.find("rgb") != std::string::npos || lower.find("color") != std::string::npos ||
        lower.find("colour") != std::string::npos) {
        *evidence = "name contains an RGB/colour token";
        return StreamCandidate::kRgb;
    }
    *evidence = "the name has no stream token";
    return StreamCandidate::kUnknown;
}

bool IsVideoStreaming(const InterfaceObservation& observation) {
    return observation.class_code == kClassVideo && observation.subclass == kSubclassVideoStreaming;
}

CorrelatedDescriptor DescriptorFor(const ProbeResult* descriptor, int interface_number) {
    CorrelatedDescriptor fields;
    if (descriptor == nullptr) return fields;
    for (const ProbeInterface& interface : descriptor->interfaces) {
        if (interface.number != interface_number) continue;
        fields.present = true;
        fields.pixel_format = interface.format_fourcc;
        fields.bytes_per_line = interface.bytes_per_line;
        fields.width = interface.width;
        fields.height = interface.height;
        if (interface.streaming_endpoint_address != kUnset) {
            fields.endpoint_address = interface.streaming_endpoint_address;
            for (const ProbeEndpoint& endpoint : interface.endpoints) {
                if (endpoint.address == interface.streaming_endpoint_address) {
                    fields.transfer = endpoint.transfer;
                }
            }
        } else if (!interface.endpoints.empty()) {
            fields.endpoint_address = interface.endpoints[0].address;
            fields.transfer = interface.endpoints[0].transfer;
        }
        break;
    }
    return fields;
}

}  // namespace

const char* StreamCandidateName(StreamCandidate candidate) {
    switch (candidate) {
        case StreamCandidate::kIr: return "ir";
        case StreamCandidate::kDepth: return "depth";
        case StreamCandidate::kRgb: return "rgb";
        default: return "unknown";
    }
}

CorrelationResult Correlate(const std::vector<InterfaceObservation>& observations,
                            const ProbeResult* descriptor) {
    CorrelationResult result;
    if (observations.empty()) {
        result.error = "no USB interfaces were observed (is the scanner attached?)";
        return result;
    }

    std::vector<const InterfaceObservation*> streaming;
    for (const InterfaceObservation& observation : observations) {
        if (IsVideoStreaming(observation)) {
            streaming.push_back(&observation);
        } else if (observation.class_code == kClassHid) {
            result.hid.push_back(observation);
        }
        if (observation.alternate_setting > 0) result.saw_any_active = true;
    }

    const StreamCandidate kinds[] = {StreamCandidate::kIr, StreamCandidate::kDepth, StreamCandidate::kRgb};
    std::vector<int> assigned;
    for (StreamCandidate kind : kinds) {
        std::vector<const InterfaceObservation*> matches;
        std::string evidence;
        for (const InterfaceObservation* observation : streaming) {
            std::string why;
            if (ClassifyName(observation->name, &why) != kind) continue;
            matches.push_back(observation);
            evidence = why;
        }
        if (matches.empty()) continue;
        CorrelatedStream stream;
        stream.candidate = kind;
        stream.evidence = evidence;
        if (matches.size() == 1) {
            const InterfaceObservation& observation = *matches.front();
            stream.interface_number = observation.number;
            stream.alternate_setting = observation.alternate_setting;
            stream.class_code = observation.class_code;
            stream.subclass = observation.subclass;
            stream.num_endpoints = observation.num_endpoints;
            stream.name = observation.name;
            stream.active_now = observation.alternate_setting > 0;
            stream.descriptor = DescriptorFor(descriptor, observation.number);
            assigned.push_back(observation.number);
        } else {
            // Two interfaces match one stream: report both, choose neither.
            stream.ambiguous = true;
            for (const InterfaceObservation* observation : matches) {
                stream.candidates.push_back(observation->number);
            }
            stream.evidence += " (more than one interface matches)";
        }
        result.streams.push_back(stream);
    }

    for (const InterfaceObservation* observation : streaming) {
        if (std::find(assigned.begin(), assigned.end(), observation->number) != assigned.end()) continue;
        bool matched_ambiguously = false;
        for (const CorrelatedStream& stream : result.streams) {
            if (!stream.ambiguous) continue;
            if (std::find(stream.candidates.begin(), stream.candidates.end(), observation->number) !=
                stream.candidates.end()) {
                matched_ambiguously = true;
            }
        }
        if (!matched_ambiguously) result.unassigned.push_back(*observation);
    }
    return result;
}

std::string FormatCorrelation(const CorrelationResult& result) {
    std::string text;
    auto append = [&text](const char* format, ...) {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        if (written > 0) {
            text.append(buffer, std::min<size_t>(static_cast<size_t>(written), sizeof(buffer) - 1));
        }
    };
    if (!result.error.empty()) {
        append("[match] error: %s\n", result.error.c_str());
        return text;
    }
    for (const CorrelatedStream& stream : result.streams) {
        if (stream.ambiguous) {
            append("[match] %s: ambiguous — interfaces", StreamCandidateName(stream.candidate));
            for (int candidate : stream.candidates) append(" %d", candidate);
            append(" (%s); not assigned\n", stream.evidence.c_str());
            continue;
        }
        // The human line, then the exact table field names for a paste.
        append("[match] %s: interface %d alt %d class=0x%02X/0x%02X name=\"%s\" (%s)\n",
               StreamCandidateName(stream.candidate), stream.interface_number, stream.alternate_setting,
               stream.class_code, stream.subclass, stream.name.c_str(), stream.evidence.c_str());
        append("[match] %s.endpoint.interface_number=%d\n", StreamCandidateName(stream.candidate),
               stream.interface_number);
        append("[match] %s.endpoint.alternate_setting=%d%s\n", StreamCandidateName(stream.candidate),
               stream.alternate_setting,
               stream.alternate_setting == 0
                   ? "  (0 at the sample: no stream active, or the registry does not track SET_INTERFACE)"
                   : "  (the streaming alternate setting at the sample)");
        if (stream.descriptor.present) {
            append("[match] %s.descriptor format=0x%08X %dx%d stride=%u", StreamCandidateName(stream.candidate),
                   stream.descriptor.pixel_format, stream.descriptor.width, stream.descriptor.height,
                   stream.descriptor.bytes_per_line);
            if (stream.descriptor.endpoint_address != kUnset) {
                append(" endpoint=0x%02X transfer=%s", stream.descriptor.endpoint_address,
                       TransferTypeName(stream.descriptor.transfer));
            }
            append("\n");
        }
    }
    for (const InterfaceObservation& observation : result.hid) {
        append("[match] hid: interface=%d alt=%d name=\"%s\"\n", observation.number,
               observation.alternate_setting, observation.name.c_str());
    }
    for (const InterfaceObservation& observation : result.unassigned) {
        append("[match] unassigned: interface=%d alt=%d class=0x%02X/0x%02X name=\"%s\"\n",
               observation.number, observation.alternate_setting, observation.class_code, observation.subclass,
               observation.name.c_str());
    }
    if (!result.saw_any_active) {
        append("[match] note: every VideoStreaming interface was at alternate setting 0 — no stream was "
               "active at this sample, or the registry does not track SET_INTERFACE\n");
    }
    append("[match] note: control (laser/exposure) is not assigned from the HID interface; it needs the "
           "captured report map\n");
    return text;
}

void PrintCorrelation(const CorrelationResult& result, std::FILE* out) {
    const std::string text = FormatCorrelation(result);
    std::fwrite(text.data(), 1, text.size(), out);
    std::fflush(out);
}

std::vector<std::string> WatchTracker::Update(const std::vector<InterfaceObservation>& observations) {
    std::vector<std::string> lines;
    auto find_previous = [this](int number) -> const InterfaceObservation* {
        for (const InterfaceObservation& observation : previous_) {
            if (observation.number == number) return &observation;
        }
        return nullptr;
    };
    for (const InterfaceObservation& observation : observations) {
        char buffer[512];
        const InterfaceObservation* previous = find_previous(observation.number);
        if (!have_previous_) {
            std::snprintf(buffer, sizeof(buffer), "= interface %d alt=%d name=\"%s\"", observation.number,
                          observation.alternate_setting, observation.name.c_str());
            lines.push_back(buffer);
            continue;
        }
        if (previous == nullptr) {
            std::snprintf(buffer, sizeof(buffer), "+ interface %d alt=%d name=\"%s\"", observation.number,
                          observation.alternate_setting, observation.name.c_str());
            lines.push_back(buffer);
            continue;
        }
        if (previous->alternate_setting != observation.alternate_setting) {
            std::snprintf(buffer, sizeof(buffer), "~ interface %d alt %d -> %d name=\"%s\"",
                          observation.number, previous->alternate_setting, observation.alternate_setting,
                          observation.name.c_str());
            lines.push_back(buffer);
        } else if (previous->name != observation.name) {
            std::snprintf(buffer, sizeof(buffer), "~ interface %d name \"%s\" -> \"%s\"", observation.number,
                          previous->name.c_str(), observation.name.c_str());
            lines.push_back(buffer);
        }
    }
    if (have_previous_) {
        for (const InterfaceObservation& previous : previous_) {
            bool present = false;
            for (const InterfaceObservation& observation : observations) {
                if (observation.number == previous.number) present = true;
            }
            if (!present) {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), "- interface %d (gone)", previous.number);
                lines.push_back(buffer);
            }
        }
    }
    previous_ = observations;
    have_previous_ = true;
    return lines;
}

}  // namespace raw_capture
