// Row #182's device-free test: the correlation logic and the watch tracker.
// The observations are synthetic (the names the device reports are the same
// shape: "… IR Camera", "… Depth Camera", "… RGB Camera", "… IMU").
#include "correlation.h"
#include "descriptor_probe.h"

#include <cstdio>
#include <string>
#include <vector>

namespace rc = raw_capture;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

rc::InterfaceObservation Obs(int number, int alt, int class_code, int subclass, int endpoints,
                             const std::string& name) {
    rc::InterfaceObservation observation;
    observation.number = number;
    observation.alternate_setting = alt;
    observation.class_code = class_code;
    observation.subclass = subclass;
    observation.num_endpoints = endpoints;
    observation.name = name;
    return observation;
}

std::vector<rc::InterfaceObservation> Scanner() {
    return {
        Obs(0, 0, 0x0E, 0x01, 0, "Creality Raptor Pro Depth Camera"),
        Obs(1, 0, 0x0E, 0x02, 1, "Creality Raptor Pro Depth Camera"),
        Obs(2, 0, 0x0E, 0x01, 0, "Creality Raptor Pro IR Camera"),
        Obs(3, 0, 0x0E, 0x02, 1, "Creality Raptor Pro IR Camera"),
        Obs(4, 0, 0x0E, 0x01, 0, "Creality Raptor Pro RGB Camera"),
        Obs(5, 0, 0x0E, 0x02, 1, "Creality Raptor Pro RGB Camera"),
        Obs(6, 0, 0x03, 0x00, 1, "Creality Raptor Pro IMU"),
    };
}

const rc::CorrelatedStream* Find(const rc::CorrelationResult& result, rc::StreamCandidate kind) {
    for (const auto& stream : result.streams) {
        if (stream.candidate == kind) return &stream;
    }
    return nullptr;
}

}  // namespace

int main() {
    // --- the names resolve IR, depth and RGB ------------------------------
    {
        const rc::CorrelationResult result = rc::Correlate(Scanner(), nullptr);
        Check(result.error.empty(), "the scanner's interface set correlates");
        const rc::CorrelatedStream* ir = Find(result, rc::StreamCandidate::kIr);
        const rc::CorrelatedStream* depth = Find(result, rc::StreamCandidate::kDepth);
        const rc::CorrelatedStream* rgb = Find(result, rc::StreamCandidate::kRgb);
        Check(ir != nullptr && ir->interface_number == 3, "IR resolves to VS interface 3");
        Check(depth != nullptr && depth->interface_number == 1, "depth resolves to VS interface 1");
        Check(rgb != nullptr && rgb->interface_number == 5, "RGB resolves to VS interface 5");
        Check(ir != nullptr && !ir->ambiguous && !depth->ambiguous, "neither is ambiguous");
        Check(result.hid.size() == 1 && result.hid[0].number == 6, "the IMU HID interface is reported separately");
        Check(result.unassigned.empty(), "nothing is left unassigned");
        Check(!result.saw_any_active, "no stream is marked active at alt 0");
        Check(ir != nullptr && ir->evidence.find("IR") != std::string::npos,
              "the assignment carries its evidence");
    }

    // --- the descriptor data is attached when a probe result is given -----
    {
        rc::ProbeResult descriptor;
        rc::ProbeInterface vs;
        vs.number = 3;
        vs.video_streaming = true;
        vs.width = 640;
        vs.height = 480;
        vs.bytes_per_line = 640;
        vs.format_fourcc = 0x59455247u;
        vs.streaming_endpoint_address = 0x83;
        rc::ProbeEndpoint endpoint;
        endpoint.address = 0x83;
        endpoint.transfer = rc::TransferType::kBulk;
        endpoint.max_packet_size = 1024;
        vs.endpoints.push_back(endpoint);
        descriptor.interfaces.push_back(vs);

        const rc::CorrelationResult result = rc::Correlate(Scanner(), &descriptor);
        const rc::CorrelatedStream* ir = Find(result, rc::StreamCandidate::kIr);
        Check(ir != nullptr && ir->descriptor.present, "the IR stream picks up its descriptor");
        Check(ir != nullptr && ir->descriptor.width == 640 && ir->descriptor.height == 480,
              "the descriptor's frame size is attached");
        Check(ir != nullptr && ir->descriptor.bytes_per_line == 640, "the descriptor's stride is attached");
        Check(ir != nullptr && ir->descriptor.endpoint_address == 0x83, "the descriptor's endpoint is attached");
        const rc::CorrelatedStream* depth = Find(result, rc::StreamCandidate::kDepth);
        Check(depth != nullptr && !depth->descriptor.present, "an interface without descriptor data is not invented");
    }

    // --- no names: nothing is guessed -------------------------------------
    {
        std::vector<rc::InterfaceObservation> anonymous = {
            Obs(1, 0, 0x0E, 0x02, 1, ""),
            Obs(3, 0, 0x0E, 0x02, 1, "USB Camera"),
        };
        const rc::CorrelationResult result = rc::Correlate(anonymous, nullptr);
        Check(result.streams.empty(), "no stream is assigned without a name token");
        Check(result.unassigned.size() == 2, "both VS interfaces are reported unassigned");
    }

    // --- two interfaces matching one stream: ambiguous, not chosen --------
    {
        std::vector<rc::InterfaceObservation> ambiguous = {
            Obs(2, 0, 0x0E, 0x02, 1, "IR Camera A"),
            Obs(3, 1, 0x0E, 0x02, 1, "IR Camera B"),
        };
        const rc::CorrelationResult result = rc::Correlate(ambiguous, nullptr);
        const rc::CorrelatedStream* ir = Find(result, rc::StreamCandidate::kIr);
        Check(ir != nullptr && ir->ambiguous, "two IR names make the assignment ambiguous");
        Check(ir != nullptr && ir->interface_number == rc::kUnset, "no interface number is chosen");
        Check(ir != nullptr && ir->candidates.size() == 2, "both candidates are listed");
    }

    // --- an active alternate setting is observed, not inferred ------------
    {
        std::vector<rc::InterfaceObservation> active = Scanner();
        active[3].alternate_setting = 1;  // the IR VS interface
        const rc::CorrelationResult result = rc::Correlate(active, nullptr);
        const rc::CorrelatedStream* ir = Find(result, rc::StreamCandidate::kIr);
        Check(ir != nullptr && ir->active_now, "the IR stream is marked active at alt 1");
        Check(result.saw_any_active, "the result records that a stream was active");
        const rc::CorrelatedStream* depth = Find(result, rc::StreamCandidate::kDepth);
        Check(depth != nullptr && !depth->active_now, "the depth stream stays inactive");
    }

    // --- the printout is paste-able table fields --------------------------
    {
        std::vector<rc::InterfaceObservation> active = Scanner();
        active[3].alternate_setting = 1;
        const std::string text = rc::FormatCorrelation(rc::Correlate(active, nullptr));
        Check(text.find("ir.endpoint.interface_number=3") != std::string::npos,
              "the printout names ir.endpoint.interface_number");
        Check(text.find("ir.endpoint.alternate_setting=1") != std::string::npos,
              "the printout names the streaming alternate setting");
        Check(text.find("depth.endpoint.interface_number=1") != std::string::npos,
              "the printout names depth.endpoint.interface_number");
        Check(text.find("hid: interface=6") != std::string::npos, "the printout reports the HID interface");
        Check(text.find("control (laser/exposure) is not assigned") != std::string::npos,
              "the printout does not claim the control interface");
    }

    // --- the watch tracker -----------------------------------------------
    {
        rc::WatchTracker tracker;
        const std::vector<std::string> baseline = tracker.Update(Scanner());
        Check(baseline.size() == Scanner().size(), "the first update is the baseline");
        Check(baseline[0].rfind("= interface 0", 0) == 0, "the baseline line names the interface");

        Check(tracker.Update(Scanner()).empty(), "no change -> no lines");

        std::vector<rc::InterfaceObservation> active = Scanner();
        active[3].alternate_setting = 1;
        const std::vector<std::string> changed = tracker.Update(active);
        Check(changed.size() == 1 && changed[0].find("interface 3 alt 0 -> 1") != std::string::npos,
              "an alternate-setting change is the transition line");

        std::vector<rc::InterfaceObservation> removed = active;
        removed.pop_back();
        const std::vector<std::string> gone = tracker.Update(removed);
        Check(gone.size() == 1 && gone[0].find("- interface 6 (gone)") != std::string::npos,
              "a removed interface is reported");

        std::vector<rc::InterfaceObservation> added = removed;
        added.push_back(Obs(7, 0, 0x0E, 0x02, 1, "Depth Camera B"));
        const std::vector<std::string> appeared = tracker.Update(added);
        Check(appeared.size() == 1 && appeared[0].find("+ interface 7") != std::string::npos,
              "a new interface is reported");
    }

    // --- the registry reader is a named refusal off macOS (or reads here) -
    {
        std::vector<rc::InterfaceObservation> interfaces;
        std::string error;
#if !defined(__APPLE__)
        Check(!rc::ReadScannerInterfaces(&interfaces, &error), "the IORegistry reader refuses off macOS");
        Check(error.find("not-configured") == 0, "the refusal is named");
#else
        std::printf("note: ReadScannerInterfaces is exercised by the live probe on macOS\n");
#endif
    }

    std::printf("[raw-capture-correlation] %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
