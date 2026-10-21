// Row #178: the non-macOS stubs for the live backends. The real ones are
// iokit_source.mm / iokit_probe.mm (macOS only); on any other platform the
// seams answer with a named refusal, never a stub that looks like success.

#include "correlation.h"
#include "descriptor_probe.h"
#include "raw_capture.h"

#if !defined(__APPLE__)

namespace raw_capture {

std::unique_ptr<FrameSource> MakeLiveUvcSource(StreamKind, std::string* why) {
    if (why) *why = "not-configured: the live UVC backend is macOS-only";
    return nullptr;
}

std::unique_ptr<CommandTransport> MakeLiveHidTransport(std::string* why) {
    if (why) *why = "not-configured: the live HID backend is macOS-only";
    return nullptr;
}

bool ProbeAttachedDevice(ProbeResult* result) {
    result->error = "not-configured: the IOKit probe is macOS-only";
    return false;
}

bool ReadScannerInterfaces(std::vector<InterfaceObservation>* interfaces, std::string* error) {
    interfaces->clear();
    *error = "not-configured: the IORegistry reader is macOS-only";
    return false;
}

}  // namespace raw_capture

#endif  // !__APPLE__
