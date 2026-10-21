// Row #182: correlate the scanner's UVC VS interfaces with the RPC streams.
//
// The #178 probe reads the descriptors but cannot say which VideoStreaming
// interface is IR and which is depth (OQ-169e). The device's own interface
// names and the live alternate settings do say it, read from the IORegistry
// without opening the device — so the correlation works while the vendor
// daemon holds the USB device, which is exactly when the streams exist.
//
// This module is pure: observations in, assignment out. The macOS reader is
// iokit_registry.mm; the test drives synthetic observations. It never guesses:
// a name it cannot classify is `unassigned`, two interfaces matching the same
// stream are `ambiguous`, and a field no source supplies stays unknown.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "descriptor_probe.h"

namespace raw_capture {

// One interface as the IORegistry reports it (no open, no claim).
struct InterfaceObservation {
    int number = kUnset;
    int alternate_setting = kUnset;
    int class_code = 0;
    int subclass = 0;
    int num_endpoints = 0;
    std::string name;  // the interface's string descriptor, when the registry has it
};

enum class StreamCandidate { kIr, kDepth, kRgb, kUnknown };

const char* StreamCandidateName(StreamCandidate candidate);

// The descriptor fields copied in when a probe result is available.
struct CorrelatedDescriptor {
    bool present = false;
    uint32_t pixel_format = 0;
    uint32_t bytes_per_line = 0;
    int width = 0;
    int height = 0;
    int endpoint_address = kUnset;
    TransferType transfer = TransferType::kUnknown;
};

struct CorrelatedStream {
    StreamCandidate candidate = StreamCandidate::kUnknown;
    int interface_number = kUnset;
    int alternate_setting = kUnset;
    int class_code = 0;
    int subclass = 0;
    int num_endpoints = 0;
    std::string name;
    std::string evidence;      // why this interface was assigned (the token in the name)
    bool active_now = false;   // alternate_setting > 0 at the observation
    bool ambiguous = false;
    std::vector<int> candidates;  // every interface that matched this candidate
    CorrelatedDescriptor descriptor;
};

struct CorrelationResult {
    std::string error;  // empty = ok
    std::vector<CorrelatedStream> streams;   // ir, depth, rgb (only those found)
    std::vector<InterfaceObservation> unassigned;  // VS interfaces with no classifiable name
    std::vector<InterfaceObservation> hid;         // class 3 interfaces (the IMU, per the name)
    bool saw_any_active = false;
};

// Classifies the observations by their interface names, pairs each with the
// probe's descriptor data (by interface number) when `descriptor` is given,
// and reports ambiguity instead of choosing.
CorrelationResult Correlate(const std::vector<InterfaceObservation>& observations,
                            const ProbeResult* descriptor);

std::string FormatCorrelation(const CorrelationResult& result);
void PrintCorrelation(const CorrelationResult& result, std::FILE* out);

// Reads the attached scanner's IOUSBHostInterface observations from the
// IORegistry (macOS): no open, no claim, no traffic, so it works while the
// vendor daemon holds the device. On a non-macOS build it returns false with
// a named reason.
bool ReadScannerInterfaces(std::vector<InterfaceObservation>* interfaces, std::string* error);

// The live watch: successive observation sets in, the transition lines out.
class WatchTracker {
public:
    // The lines describing what changed since the previous call (empty when
    // nothing did). The first call reports the baseline.
    std::vector<std::string> Update(const std::vector<InterfaceObservation>& observations);

private:
    bool have_previous_ = false;
    std::vector<InterfaceObservation> previous_;
};

}  // namespace raw_capture
